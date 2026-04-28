# Plan implementacji dashboardu Pico W

## Architektura
- STM32F103 (centralny, z DS3231 RTC) zbiera pomiary z zewnetrznych stacji.
- STM32 wysyla dane do Pico W przez UART (co 1 minute).
- Pico W zapisuje dane na SD i udostepnia API + strone z wykresami.

## Protokol UART
Jedna linia ASCII na stacje:

YYYY-MM-DDTHH:MM:SS,<station_id>,<si7021_temp>,<si7021_hum>,<bmp280_temp>,<bmp280_press>,<tsl2561_lux>,<status>\n
Przyklad:

2026-04-26T14:23:00,S1,23.4,65.2,23.1,1013.25,450,OK

## Zakres implementacji (etap 1)
1. Rozbudowa blink.py:
   - odbior ramek UART,
   - walidacja/parsowanie danych,
   - zapis JSON Lines na SD per stacja,
   - API: /api/stations, /api/data,
   - tryb symulacji danych.
2. Przepisanie index.html:
   - wybor stacji i zakresu czasu,
   - 5 wykresow Chart.js,
   - tabela biezacych odczytow i statusow,
   - auto refresh.
3. Dodanie simulate.py:
   - generator danych testowych co 60 s,
   - format ramek zgodny z UART.

## Kolejny etap
- Dodanie endpointu triggerujacego pomiar po stronie STM32 (komenda UART MEASURE\n).
