---
name: Naprawa odbioru NRF
overview: Naprawimy regresję konfiguracji stacji BME oraz utwardzimy opróżnianie RX FIFO po obu stronach. Identyfikacja `S1` z `NRF:RX_PIPE=2` jest poprawna; problemem jest lokalny pipe komend OutdoorUnit oraz obsługa kolejnych wpisów FIFO, a nie szybszy pomiar sam w sobie.
todos:
  - id: fix-command-pipe
    content: Przywrócić wspólny pipe 1 dla komend OutdoorUnit i poprawić opis konfiguracji
    status: pending
  - id: harden-rx-fifo
    content: Poprawić drenowanie oraz diagnostykę RX FIFO w OutdoorUnit i IndoorUnit
    status: pending
  - id: verify-build-runtime
    content: Uruchomić test protokołu, buildy i przygotować kryteria testu dwóch stacji
    status: pending
isProject: false
---

# Naprawa odbioru NRF

## Ustalenia
- `NRF:RX_PIPE=2` w IndoorUnit poprawnie oznacza `NODE_ID=1`; dlatego linia `DATA:...S1...` nie pochodziła z node 0. `CYCLE_RECV=0x02` potwierdza, że przed timeoutem zaakceptowano wyłącznie node 1.
- Regresja znajduje się w [measurement_unit_config.h](/home/tepe/programowanie/stm32/Weather-Station/OutdoorUnit/Core/Inc/measurement_unit_config.h): ostatnia zmiana ustawiła `NRF_PIPE_CMD=2` dla stacji BME. Pipe jest lokalnym numerem odbiornika, nie identyfikatorem node.
- [NRF24L01.c](/home/tepe/programowanie/stm32/Weather-Station/OutdoorUnit/Core/Src/NRF24L01.c) zapisuje dla pipe 2 tylko `addr[0]`; pozostałe cztery bajty są dziedziczone z `RX_ADDR_P1`. OutdoorUnit nie ustawia wtedy pipe 1 na adres broadcast, więc stacja BME nie nasłuchuje pewnie na `{0xB0,...}`. To bezpośrednio tłumaczy asymetrię między node 0 i node 1.
- Pętle RX w [outdoor_station.c](/home/tepe/programowanie/stm32/Weather-Station/OutdoorUnit/Core/Src/outdoor_station.c) i [weather_station.c](/home/tepe/programowanie/stm32/Weather-Station/IndoorUnit/Core/Src/weather_station.c) kończą drenowanie na podstawie `RX_DR`. Flaga IRQ nie jest wiarygodnym odpowiednikiem „FIFO niepuste”, więc drugi pakiet może zostać pominięty. IndoorUnit dodatkowo bez logu odrzuca ramkę, której dekodowanie się nie powiedzie.

## Zmiany
- W [measurement_unit_config.h](/home/tepe/programowanie/stm32/Weather-Station/OutdoorUnit/Core/Inc/measurement_unit_config.h) przywrócić wspólny `NRF_PIPE_CMD=1U` dla wszystkich OutdoorUnit, pozostawiając bieżące niezatwierdzone `NODE_ID=1U`, i poprawić komentarze rozdzielające lokalny pipe komend od pipe odpowiedzi w IndoorUnit.
- W [outdoor_station.c](/home/tepe/programowanie/stm32/Weather-Station/OutdoorUnit/Core/Src/outdoor_station.c) opróżniać RX FIFO do `RX_EMPTY`, odczytywać numer pipe przed payloadem, dekodować komendę tylko z `NRF_PIPE_CMD`, a nieoczekiwany pipe odrzucać z jednoznacznym logiem. `RX_DR` wyczyścić po drenowaniu FIFO.
- W [weather_station.c](/home/tepe/programowanie/stm32/Weather-Station/IndoorUnit/Core/Src/weather_station.c) zastosować ten sam wzorzec drenowania do `RX_EMPTY`, walidować pipe przed mapowaniem na node i dodać log dla odrzuconej ramki/pipe. Dzięki temu szybsza odpowiedź node 1 nie może pozostawić późniejszej odpowiedzi node 0 nieobsłużonej tylko dlatego, że flaga `RX_DR` została wcześniej skasowana.
- Nie rozszerzać teraz protokołu o `cycle_id` w odpowiedzi: to osobne ryzyko spóźnionych ramek, ale nie wyjaśnia aktualnej regresji i wymagałoby niepotrzebnej zmiany formatu po obu stronach.

## Weryfikacja
- Zbudować IndoorUnit i OutdoorUnit z bieżącą konfiguracją `NODE_ID=1`, uruchomić hostowy `test/test_ws_cmd_protocol.py` i sprawdzić brak nowych diagnostyk lintera.
- Test sprzętowy: po pełnym odłączeniu zasilania obu stacji oczekiwać w każdym cyklu `RX_PIPE=2/S1`, następnie `RX_PIPE=1/S0`, `CYCLE_RECV=0x03` i `CYCLE_COMPLETE`, bez `RX_DROP_DECODE` oraz `CYCLE_MISS`.
- Jeżeli pozostanie drop, nowe logi pipe/decode rozstrzygną, czy źródłem jest nieoczekiwany pipe, uszkodzony payload czy brak transmisji node 0.