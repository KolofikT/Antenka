#pragma once

#include <Arduino.h>
#include <functional>
#include <cmath>
#include "robotka.h"

struct MoveResult {
    bool success;
    float traveled_mm; // Skutečně ujetá vzdálenost se znaménkem (+ vpřed, - vzad)
};

/**
 * \brief Plynulý pohyb s vyhýbáním se (robot nenarazí).
 * 
 * \param mm Vzdálenost v milimetrech (kladná pro jízdu vpřed, záporná pro vzad).
 * \param speed Rychlost v % (0-100).
 * \param is_obstacle Funkce/lambda, která vrátí true, pokud je detekována překážka.
 * \param wait_timeout_ms Jak dlouho (v ms) robot maximálně počká, než vyhodí chybu (výchozí 5s).
 * \return MoveResult struktura s úspěšností (success) a skutečně ujetou vzdáleností (traveled_mm).
 */
inline MoveResult move_acc_avoid(float mm, float speed, std::function<bool()> is_obstacle, uint32_t wait_timeout_ms = 5000) {
    if (mm == 0 || speed == 0) { return {true, 0.0f}; }
    
    // Podpora pro jízdu pozpátku
    bool reverse = (mm < 0);
    float target_mm = std::abs(mm); // Cílová vzdálenost bude vždy kladná (pro výpočet ušlé dráhy)
    
    // Normalizace rychlosti (aby znaménko rychlosti odpovídalo směru)
    float abs_target_speed = std::abs(speed);
    float speed_sign = reverse ? -1.0f : 1.0f;
    
    // Vynulování odometrie
    rkMotorsSetPositionLeft(0);
    rkMotorsSetPositionRight(0);
    delay(50); // Krátká pauza, aby se koprocesor stihl zresetovat
    
    float min_speed = 18.0f;
    if (abs_target_speed < min_speed) { abs_target_speed = min_speed; }
    
    float current_base_speed = min_speed * speed_sign;
    
    // Parametry rampy a P-regulátoru
    float decel_distance_mm = 2.5f * abs_target_speed; // Plynulé brzdění (při rychlosti 40% brzdí už 100 mm před cílem)
    float accel_step = abs_target_speed / 100.0f; // Jemná akcelerace (rozjezd na max. rychlost zabere 1 sekundu)
    float decel_step = abs_target_speed / 100.0f; // Jemné zpomalování bez smyku kol
    float avoid_decel_step = abs_target_speed / 5.0f; // Rychlé zastavení před překážkou (cca 50 ms)
    
    float kp = 0.8f; // Proporcionální konstanta pro P-regulátor na milimetry
    float max_corr = 8.0f; // Maximální zásah regulátoru (%)
    
    unsigned long start_time = millis();
    unsigned long avoid_wait_start = 0;
    bool waiting_for_obstacle = false;
    
    // Výpočet hrubého timeoutu pro celou cestu
    float speed_mm_per_sec = abs_target_speed * 5.0f; // odhad 100% speed ~ 500 mm/s
    float expected_time_ms = (target_mm / speed_mm_per_sec) * 1000.0f;
    uint32_t general_timeout_ms = (uint32_t)(expected_time_ms * 2.0f + 3000.0f);
    
    // Pomocná funkce pro bezpečné ořezání hodnoty a převod do int8_t
    auto clamp_speed = [](float s) -> int8_t {
        if (s > 100.0f) return 100;
        if (s < -100.0f) return -100;
        return static_cast<int8_t>(s);
    };

    float avg_pos = 0.0f;

    while (true) {
        float pos_l = rkMotorsGetPositionLeft(true);
        float pos_r = rkMotorsGetPositionRight(true);
        float abs_l = std::abs(pos_l);
        float abs_r = std::abs(pos_r);
        avg_pos = (abs_l + abs_r) / 2.0f;
        
        if (avg_pos >= target_mm) {
            avg_pos = target_mm; // Oříznutí na cíl pro přesnost návratové hodnoty
            break; // Dojeli jsme do cíle
        }
        
        // Časový limit pro celou funkci (pokud se někde nezasekne natrvalo)
        if ((millis() - start_time > general_timeout_ms) && !waiting_for_obstacle) {
            rkMotorsSetSpeed(0, 0);
            return {false, reverse ? -avg_pos : avg_pos}; 
        }
        
        bool obstacle = is_obstacle();
        
        if (obstacle) {
            if (!waiting_for_obstacle) {
                // Fáze: Rychle plynule zastavit
                float abs_curr = std::abs(current_base_speed);
                abs_curr -= avoid_decel_step;
                if (abs_curr <= 0) {
                    abs_curr = 0;
                    waiting_for_obstacle = true;
                    avoid_wait_start = millis();
                }
                current_base_speed = abs_curr * speed_sign;
            } else {
                // Fáze: Zastaveno, čekáme na volnou cestu
                current_base_speed = 0;
                if (millis() - avoid_wait_start > wait_timeout_ms) {
                    rkMotorsSetSpeed(0, 0);
                    return {false, reverse ? -avg_pos : avg_pos}; // Překážka nezmizela v limitu
                }
            }
        } else {
            if (waiting_for_obstacle) {
                // Překážka právě zmizela, pokračujeme v jízdě
                waiting_for_obstacle = false;
                start_time += (millis() - avoid_wait_start); // Posuneme celkový timeout
                current_base_speed = min_speed * speed_sign; 
            }
            
            float dist_remaining = target_mm - avg_pos;
            if (dist_remaining <= decel_distance_mm) {
                // Fáze: Běžné plynulé zpomalování před cílem
                float abs_curr = std::abs(current_base_speed);
                abs_curr -= decel_step;
                if (abs_curr < min_speed) abs_curr = min_speed;
                current_base_speed = abs_curr * speed_sign;
            } else {
                // Fáze: Zrychlování a konstantní jízda
                float abs_curr = std::abs(current_base_speed);
                if (abs_curr < abs_target_speed) {
                    abs_curr += accel_step;
                    if (abs_curr > abs_target_speed) abs_curr = abs_target_speed;
                }
                current_base_speed = abs_curr * speed_sign;
            }
        }
        
        // Aplikace rychlosti a P-regulátoru pro udržení směru
        if (current_base_speed != 0) {
            float diff = abs_l - abs_r; // Kladné -> levé kolo je napřed
            float correction = std::max(-max_corr, std::min(diff * kp, max_corr));
            
            float speed_l = current_base_speed;
            float speed_r = current_base_speed;
            
            // Pokud je levé kolo napřed, zpomalíme levé a zrychlíme pravé
            speed_l -= correction * speed_sign; 
            speed_r += correction * speed_sign;
            
            rkMotorsSetSpeed(clamp_speed(speed_l), clamp_speed(speed_r));
        } else {
            rkMotorsSetSpeed(0, 0);
        }
        delay(10);
    }
    
    rkMotorsSetSpeed(0, 0);
    return {true, reverse ? -avg_pos : avg_pos};
}

/**
 * \brief Plynulé otočení na místě DOLEVA s pomalým rozjezdem.
 * 
 * \param angle Úhel vázaný na rotaci (ve stupních).
 * \param speed Maximální rychlost v %.
 * \param roztec_kol Šířka mezi koly (výchozí 155.0 mm).
 * \param korekce Konstanta z kalibrace (výchozí 0.947 pro levou stranu).
 */
inline void TurnOnSpotLeft_acc(float angle, float speed, float roztec_kol = 155.0f, float korekce = 0.947f) {
    if (angle <= 0 || speed <= 0) return;
    
    // Výpočet cílové dráhy v mm pro jedno kolo
    float target_mm = korekce * (M_PI * roztec_kol) * (angle / 360.0f);
    float abs_target_speed = std::abs(speed);
    
    rkMotorsSetPositionLeft(0);
    rkMotorsSetPositionRight(0);
    delay(50); // Reset koprocesoru
    
    float min_speed = 15.0f;
    if (abs_target_speed < min_speed) { abs_target_speed = min_speed; }
    float current_base_speed = min_speed;
    
    // === NASTAVENÍ RAMP ===
    float decel_distance_mm = 4.0f * abs_target_speed; // Delší brzdná dráha (o 60 % delší)
    float accel_step = abs_target_speed / 100.0f; // Jemná akcelerace
    float decel_step = abs_target_speed / 200.0f; // Poloviční zpomalení (klouže déle)
    
    float kp = 0.8f; 
    float max_corr = 8.0f;
    
    unsigned long start_time = millis();
    uint32_t general_timeout_ms = 10000; // Timeout 10 vteřin
    
    auto clamp_speed = [](float s) -> int8_t {
        if (s > 100.0f) return 100;
        if (s < -100.0f) return -100;
        return static_cast<int8_t>(s);
    };

    float avg_pos = 0.0f;

    while (true) {
        float pos_l = rkMotorsGetPositionLeft(true);
        float pos_r = rkMotorsGetPositionRight(true);
        float abs_l = std::abs(pos_l);
        float abs_r = std::abs(pos_r);
        avg_pos = (abs_l + abs_r) / 2.0f;
        
        if (avg_pos >= target_mm) break;
        if (millis() - start_time > general_timeout_ms) break;
        
        float dist_remaining = target_mm - avg_pos;
        
        if (dist_remaining <= decel_distance_mm) {
            current_base_speed -= decel_step;
            if (current_base_speed < min_speed) current_base_speed = min_speed;
        } else {
            if (current_base_speed < abs_target_speed) {
                current_base_speed += accel_step;
                if (current_base_speed > abs_target_speed) current_base_speed = abs_target_speed;
            }
        }
        
        // P-regulátor na přesný střed otáčení (obě kola ujedou absolutně stejnou vzdálenost)
        float diff = abs_l - abs_r; // Kladné -> levé ujelo víc
        float correction = std::max(-max_corr, std::min(diff * kp, max_corr));
        
        float speed_l_mag = current_base_speed - correction;
        float speed_r_mag = current_base_speed + correction;
        
        // DOLEVA = levé kolo couvá, pravé jede vpřed
        rkMotorsSetSpeed(clamp_speed(-speed_l_mag), clamp_speed(speed_r_mag));
        delay(10);
    }
    rkMotorsSetSpeed(0, 0);
}

/**
 * \brief Plynulé otočení na místě DOPRAVA s pomalým rozjezdem.
 * 
 * \param angle Úhel vázaný na rotaci (ve stupních).
 * \param speed Maximální rychlost v %.
 * \param roztec_kol Šířka mezi koly (výchozí 155.0 mm).
 * \param korekce Konstanta z kalibrace (výchozí 0.973 pro pravou stranu).
 */
inline void TurnOnSpotRight_acc(float angle, float speed, float roztec_kol = 155.0f, float korekce = 0.973f) {
    if (angle <= 0 || speed <= 0) return;
    
    float target_mm = korekce * (M_PI * roztec_kol) * (angle / 360.0f);
    float abs_target_speed = std::abs(speed);
    
    rkMotorsSetPositionLeft(0);
    rkMotorsSetPositionRight(0);
    delay(50);
    
    float min_speed = 15.0f;
    if (abs_target_speed < min_speed) { abs_target_speed = min_speed; }
    float current_base_speed = min_speed;
    
    // === NASTAVENÍ RAMP ===
    float decel_distance_mm = 4.0f * abs_target_speed; 
    float accel_step = abs_target_speed / 100.0f; 
    float decel_step = abs_target_speed / 200.0f; 
    
    float kp = 0.8f; 
    float max_corr = 8.0f;
    
    unsigned long start_time = millis();
    uint32_t general_timeout_ms = 10000;
    
    auto clamp_speed = [](float s) -> int8_t {
        if (s > 100.0f) return 100;
        if (s < -100.0f) return -100;
        return static_cast<int8_t>(s);
    };

    float avg_pos = 0.0f;

    while (true) {
        float pos_l = rkMotorsGetPositionLeft(true);
        float pos_r = rkMotorsGetPositionRight(true);
        float abs_l = std::abs(pos_l);
        float abs_r = std::abs(pos_r);
        avg_pos = (abs_l + abs_r) / 2.0f;
        
        if (avg_pos >= target_mm) break;
        if (millis() - start_time > general_timeout_ms) break;
        
        float dist_remaining = target_mm - avg_pos;
        if (dist_remaining <= decel_distance_mm) {
            current_base_speed -= decel_step;
            if (current_base_speed < min_speed) current_base_speed = min_speed;
        } else {
            if (current_base_speed < abs_target_speed) {
                current_base_speed += accel_step;
                if (current_base_speed > abs_target_speed) current_base_speed = abs_target_speed;
            }
        }
        
        float diff = abs_l - abs_r;
        float correction = std::max(-max_corr, std::min(diff * kp, max_corr));
        
        float speed_l_mag = current_base_speed - correction;
        float speed_r_mag = current_base_speed + correction;
        
        // DOPRAVA = levé kolo jede vpřed, pravé couvá
        rkMotorsSetSpeed(clamp_speed(speed_l_mag), clamp_speed(-speed_r_mag));
        delay(10);
    }
    rkMotorsSetSpeed(0, 0);
}
