# Checklist MVP centralnej stacji pogodowej

Źródło: ustalenia projektowe dla Indoor Unit (STM32F103 + DS3231 + nRF24L01+ + PCD8544 + SD).

## Główna lista zadań

- [ ] Ustalić i zamrozić kontrakt ramki RF: temp, humidity, pressure, CRC, timestamp.
- [ ] Dodać warstwę schedulera zdarzeń z flagami: minute_tick, manual_measurement, storage_error, rf_error.
- [ ] Skonfigurować DS3231 do wyzwalania co 1 minutę i poprawnie czyścić flagi alarmu po obsłudze.
- [ ] Podłączyć obsługę IRQ RTC oraz przycisku do logiki aplikacyjnej (bez ciężkiej pracy w przerwaniu).
- [ ] Zaimplementować sekwencję pomiaru RF: wake -> request -> wait(1s) -> retry x3 -> validate CRC.
- [ ] Dodać mapowanie kodów błędów RF (timeout, CRC fail, no response) na status użytkowy.
- [ ] Wdrożyć logowanie CSV do pliku dziennego YYYYMMDD.csv.
- [ ] Zaimplementować politykę błędów SD: 3 próby zapisu, potem flaga błędu i komunikat w UI.
- [ ] Zdefiniować jeden format wiersza CSV i utrzymać go spójnie w całym projekcie.
- [ ] Uzupełnić menu o pozycje: MEASUREMENTS, HISTORY, TAKE MEASUREMENT, SETTINGS.
- [ ] Podłączyć akcję TAKE MEASUREMENT do tej samej ścieżki co pomiar automatyczny.
- [ ] Dodać widok MEASUREMENTS z ostatnim poprawnym rekordem i statusem systemu.
- [ ] Dodać widok HISTORY z odczytem ostatnich rekordów z logu dziennego.
- [ ] Dodać ekran statusów i błędów (RF, SD, RTC), który nie blokuje działania pętli głównej.
- [ ] Utrzymać architekturę nieblokującą: krótki main loop, logika oparta o stany i flagi.
- [ ] Upewnić się, że zmiany w plikach CubeMX są tylko w sekcjach USER CODE.
- [ ] Zbudować projekt i usunąć błędy kompilacji po każdej większej iteracji.
- [ ] Przetestować scenariusze: auto-pomiar co minutę, ręczny pomiar, timeout RF, błąd SD, restart urządzenia.
- [ ] Potwierdzić kryterium gotowości MVP: pomiar co minutę + LCD + CSV + działające menu + obsługa błędów.

## Definicja ukończenia (DoD)

MVP uznajemy za gotowe, jeżeli wszystkie punkty listy są oznaczone jako wykonane i projekt przechodzi testy scenariuszowe bez resetów oraz blokowania pętli głównej.
