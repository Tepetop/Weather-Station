---
name: Indoor energy reduction
overview: IndoorUnit już usypia nRF24 w najoszczędniejszym trybie Power Down, więc Standby-I nie jest poprawką energetyczną. Dalszą redukcję uzyskamy przez wyłączenie kontrolera LCD podczas screensavera oraz zagwarantowanie, że aktywna-nisko dioda PC13 jest zgaszona przed STOP, bez zmiany istniejącej polityki radia.
todos:
  - id: lcd-powerdown-api
    content: Dodać Doxygen API Power Down/Active do sterownika PCD8544
    status: pending
  - id: screensaver-lcd-sleep
    content: Usypiać LCD w screensaverze i poprawnie odtwarzać go po przycisku
    status: pending
  - id: fix-led-polarity
    content: Ujednoznacznić aktywną-nisko diodę i gasić ją w stanie bezczynnym
    status: pending
  - id: measure-and-verify
    content: Zbudować firmware oraz porównać prąd i pełny cykl sleep/wake
    status: pending
isProject: false
---

# Ograniczenie poboru IndoorUnit

## Ustalenie
- [`PowerMgr_EnterIdleStop()`](/home/tepe/programowanie/stm32/Weather-Station/IndoorUnit_newMCU/Core/Src/Station/power_mgr.c) wywołuje `NRF24_PowerDown()` przed STOP: `CE=0`, `PWR_UP=0`, typowo około 0,9 µA.
- Standby-I występuje tylko krótko po `PowerMgr_WakeRadio()` i pobiera około 26 µA. Użycie go podczas STOP zwiększyłoby pobór około 29 razy, dlatego kodu nRF nie należy zmieniać.

## Zmiany naprawcze
1. Rozszerzyć sterownik PCD8544 w [`PCD8544.h`](/home/tepe/programowanie/stm32/Weather-Station/IndoorUnit_newMCU/Core/Inc/Sensors/PCD_LCD/PCD8544.h) i [`PCD8544.c`](/home/tepe/programowanie/stm32/Weather-Station/IndoorUnit_newMCU/Core/Src/Sensors/PCD_LCD/PCD8544.c) o idempotentne, udokumentowane Doxygen API wejścia/wyjścia z `MODE_P_DOWN`:
   - przy usypianiu wysłać `FUNCTION_SET | MODE_P_DOWN` po wyłączeniu podświetlenia;
   - przy wybudzeniu przywrócić tryb aktywny i konfigurację kontrolera wymaganą przez PCD8544, a następnie przesłać zachowany bufor ekranu;
   - nie wykonywać pełnego resetu GPIO, który niepotrzebnie czyściłby stan UI.

2. Podłączyć LCD power-down do screensavera w [`weather_station_ui.c`](/home/tepe/programowanie/stm32/Weather-Station/IndoorUnit_newMCU/Core/Src/Station/weather_station_ui.c):
   - wejście w screensaver: backlight OFF, kontroler LCD Power Down, wyłączenie Alarm1 jak obecnie;
   - wyjście po przycisku: LCD Active, odtworzenie obrazu, backlight ON;
   - pozostawić LCD uśpiony podczas automatycznych cykli pomiarowych wybudzanych Alarm2, dopóki użytkownik nie opuści screensavera.

3. Uporządkować aktywną-nisko diodę PC13 w [`weather_station.c`](/home/tepe/programowanie/stm32/Weather-Station/IndoorUnit_newMCU/Core/Src/Station/weather_station.c):
   - zastąpić niejednoznaczne surowe `GPIO_PIN_SET/RESET` helperami logicznymi `ws_led_on()`/`ws_led_off()`;
   - LED włączać tylko na czas aktywnej komunikacji, a wyłączać po finalizacji cyklu, odebraniu danych, inicjalizacji oraz przed osiągnięciem stanu pozwalającego na STOP;
   - zachować zmianę lokalną, bez przebudowy maszyny stanów.

4. Zachować obecną implementację [`power_mgr.c`](/home/tepe/programowanie/stm32/Weather-Station/IndoorUnit_newMCU/Core/Src/Station/power_mgr.c): STOP z regulatorem low-power, nRF Power Down i WWDG zatrzymanym przez brak PCLK. Zaktualizować tylko komentarze, jeśli będą potrzebne do jednoznacznego opisania, że Standby-I występuje po wybudzeniu, nie w STOP.

## Weryfikacja
- Zbudować preset Debug i sprawdzić diagnostykę zmienionych plików.
- Zmierzyć prąd całej płytki w trzech stanach: obecny STOP, STOP z wymuszoną diodą OFF oraz STOP z diodą OFF i LCD Power Down; zaakceptować optymalizacje tylko przy mierzalnym spadku.
- Test funkcjonalny: screensaver → STOP → Alarm2 i pomiar bez budzenia LCD → ponowny STOP → przycisk → poprawne odtworzenie LCD i podświetlenia.
- Sprawdzić oscyloskopem/logiem, że przed każdym STOP nRF nadal ma `CE=0` i pozostaje w Power Down.
- Jeżeli po tych zmianach prąd nadal jest rzędu mA, kolejnym krokiem powinien być pomiar prądu karty SD. Programowe odmontowanie nie gwarantuje niskiego poboru; dalsza redukcja może wymagać sprzętowego odcinania jej zasilania.