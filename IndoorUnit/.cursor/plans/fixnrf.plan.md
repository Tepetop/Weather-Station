---
name: ""
overview: ""
todos: []
isProject: false
---

# Plan naprawy biblioteki NRF24L01 (STM32 HAL)

**Zakres:** `NRF24L01.c` / `NRF24L01.h` **Podstawa analizy:** nRF24L01+ Product Specification v1.0 (Nordic Semiconductor) **Data analizy:** 2026-07-22

Dokument opisuje błędy i luki znalezione podczas przeglądu kodu pod kątem zgodności z Enhanced ShockBurst™ i MultiCeiver™, wraz z konkretnym planem ich naprawy, kolejnością prac i sposobem weryfikacji.

---

## Priorytetyzacja


| #   | Priorytet | Problem                                                                        | Funkcja                               |
| --- | --------- | ------------------------------------------------------------------------------ | ------------------------------------- |
| 1   | 🔴 Wysoki | Zła maska bitowa — funkcja zawsze zwraca błąd                                  | `NRF24_IsPresent()`                   |
| 2   | 🟠 Średni | Brak kontroli `len > 32` przy odczycie                                         | `NRF24_ReadPayload`, `NRF24_ReadRegs` |
| 3   | 🟠 Średni | Brak wymuszenia zależności EN_ACK_PAY ↔ EN_DPL/DYNPD_P0                        | `NRF24_EnableAckPay`                  |
| 4   | 🟠 Średni | Brak ochrony przed zapisem rejestrów w trybie aktywnym                         | moduł konfiguracyjny                  |
| 5   | 🟡 Niski  | Toggle `ACTIVATE` przy każdym `Init`                                           | `NRF24_Init`                          |
| 6   | 🟡 Niski  | Zbędne opóźnienie 1500µs przy każdym wejściu w Standby                         | `NRF24_SetMode`                       |
| 7   | 🟡 Niski  | `MAX_RT` automatycznie kasuje TX FIFO (odbiera możliwość ręcznej retransmisji) | `NRF24_IRQ_Handler`                   |
| 8   | 🟡 Niski  | `NRF24_IsDataAvailable` nie waliduje `RX_P_NO` i nie drenuje FIFO do końca     | `NRF24_IsDataAvailable`               |


Kolejność prac: **1 → 2 → 3 → 4 → 5/6/7/8** (od najbardziej krytycznego do kosmetycznych).

---

## 1. 🔴 `NRF24_IsPresent()` — błędna maska bitowa

### Problem

```c
uint8_t status = NRF24_GetStatus(handle);
if ((status & 0xF0U) != 0xE0U) {
    return HAL_ERROR;
}

```

Domyślny (post-reset) `STATUS` = `0x0E` (bit7=zarezerwowany=0, `RX_P_NO`=`111` w bitach 3:1, `TX_FULL`=0). Maska `0xF0` sprawdza bity 7:4, czyli inny fragment rejestru niż ten, w którym faktycznie znajduje się wzorzec „111”. Warunek `== 0xE0` wymagałby ustawienia zarezerwowanego bitu 7 na `1`, co wg specyfikacji nigdy nie powinno wystąpić. **Efekt: funkcja praktycznie zawsze zwraca** `HAL_ERROR`**, nawet gdy chip działa poprawnie.**

### Naprawa

```c
uint8_t status = NRF24_GetStatus(handle);
// Bity 3:1 (RX_P_NO) po resecie / gdy RX FIFO puste = 0b111 -> maska 0x0E
if ((status & 0x0EU) != 0x0EU) {
    return HAL_ERROR;
}

```

### Uwaga dodatkowa

Ten test jest wiarygodny **tylko wtedy, gdy RX FIFO jest puste** (RX_P_NO=111 oznacza „RX FIFO Empty”, nie tylko stan po resecie). Jeśli `NRF24_IsPresent()` ma być wywoływane w dowolnym momencie pracy urządzenia (a nie tylko zaraz po `Init`), rozważ zamiast tego bardziej niezawodny test: zapis/odczyt rejestru testowego (`RF_CH`), tak jak już robi to dalsza część funkcji — i **usunięcie** niepewnego testu wzorca STATUS albo pozostawienie go wyłącznie jako dodatkowej, nieblokującej wskazówki diagnostycznej.

### Testy weryfikacyjne

- [ ] Po świeżym `Init()` (RX FIFO puste) `NRF24_IsPresent()` zwraca `HAL_OK` na realnym sprzęcie.
- [ ] Z odłączonym/brakującym modułem (pływające MISO) funkcja nadal zwraca `HAL_ERROR`.
- [ ] Test regresyjny: wywołanie po odebraniu i wyczyszczeniu pakietów (RX FIFO ponownie puste) — nadal `HAL_OK`.

---

## 2. 🟠 Brak walidacji długości payloadu przy odczycie

### Problem

`NRF24_WritePayload` poprawnie sprawdza górną granicę:

```c
if(handle == NULL || buf == NULL || len == 0 || len > NRF24_MAX_PAYLOAD_SIZE) {
    return HAL_ERROR;
}

```

Analogicznej kontroli brakuje w `NRF24_ReadPayload` i `NRF24_ReadRegs`:

```c
HAL_StatusTypeDef NRF24_ReadPayload(NRF24_Handle_t *handle, uint8_t *buf, uint8_t len) {
    if(handle == NULL || buf == NULL) {
        return HAL_ERROR;
    }
    return read_payload(handle, NRF24_CMD_R_RX_PAYLOAD, buf, len);
}

```

`read_payload()` alokuje VLA (`uint8_t nop[len];`) i wykonuje transfer SPI o długości `len`. Przy `len > 32` (payload wg specyfikacji ma max 32 bajty) odczytujemy dane spoza rzeczywistej zawartości FIFO, a przy dużych wartościach `len` ryzykujemy przepełnienie stosu na mikrokontrolerze.

### Naprawa

```c
HAL_StatusTypeDef NRF24_ReadPayload(NRF24_Handle_t *handle, uint8_t *buf, uint8_t len) {
    if (handle == NULL || buf == NULL || len == 0 || len > NRF24_MAX_PAYLOAD_SIZE) {
        return HAL_ERROR;
    }
    return read_payload(handle, NRF24_CMD_R_RX_PAYLOAD, buf, len);
}

```

```c
HAL_StatusTypeDef NRF24_ReadRegs(NRF24_Handle_t *handle, uint8_t reg, uint8_t *buf, uint8_t len) {
    if (handle == NULL || buf == NULL || len == 0 || len > 5) {
        // 5 = maksymalna długość dowolnego rejestru wielobajtowego (adresy RX/TX)
        return HAL_ERROR;
    }
    return read_payload(handle, NRF24_CMD_R_REGISTER | reg, buf, len);
}

```

Analogicznie warto dodać ten sam limit (`len > 5`) do `NRF24_WriteRegs`, dla spójności.

### Dodatkowe wzmocnienie (opcjonalne, zalecane)

W `read_payload()` / `write_payload()` zamienić VLA na stałej wielkości bufor (`NRF24_MAX_PAYLOAD_SIZE`), skoro i tak górna granica jest teraz wymuszana na wejściu — eliminuje to zależność od VLA na poziomie funkcji pomocniczej jako dodatkową warstwę bezpieczeństwa (defense in depth), niezależną od tego, czy ktoś w przyszłości doda nowe wywołanie `read_payload()` bez sprawdzenia długości.

### Testy weryfikacyjne

- [ ] Wywołanie `NRF24_ReadPayload(handle, buf, 33)` zwraca `HAL_ERROR` bez wykonania transferu SPI.
- [ ] Wywołanie `NRF24_ReadPayload(handle, buf, 0)` zwraca `HAL_ERROR`.
- [ ] Odczyt payloadu 32-bajtowego nadal działa poprawnie (brak regresji).
- [ ] Analogiczny test dla `NRF24_ReadRegs`.

---

## 3. 🟠 Brak wymuszenia zależności EN_ACK_PAY ↔ EN_DPL/DYNPD_P0

### Problem

Zgodnie z sekcją 7.4.1 i przypisem d w tabeli rejestrów: ACK z payloadem wymaga, by **Dynamic Payload Length był włączony także dla pipe 0**, zarówno po stronie PTX, jak i PRX. `NRF24_EnableAckPay()` ustawia wyłącznie bit `EN_ACK_PAY` w rejestrze `FEATURE`, nie sprawdzając ani nie wymuszając `EN_DPL`/`DYNPD_P0`. Błędna konfiguracja (ACK payload włączony bez DPL na pipe 0) prowadzi do niedziałającej lub niestabilnej komunikacji, trudnej do zdiagnozowania.

### Naprawa

Rozszerzyć `NRF24_EnableAckPay`, tak by przy włączaniu automatycznie zapewniał wymagane zależności:

```c
HAL_StatusTypeDef NRF24_EnableAckPay(NRF24_Handle_t *handle, uint8_t enable) {
    if (handle == NULL) return HAL_ERROR;

    uint8_t feature;
    HAL_StatusTypeDef status = NRF24_ReadReg(handle, NRF24_REG_FEATURE, &feature);
    if (status != HAL_OK) return status;

    if (enable) {
        feature |= NRF24_FEATURE_EN_ACK_PAY;
        feature |= NRF24_FEATURE_EN_DPL; // wymagane przez datasheet dla ACK payload
    } else {
        feature &= ~NRF24_FEATURE_EN_ACK_PAY;
    }

    status = NRF24_WriteReg(handle, NRF24_REG_FEATURE, feature);
    if (status != HAL_OK || !enable) return status;

    // Wymuszenie DYNPD na pipe 0 (wymagane po stronie PTX i PRX)
    return NRF24_EnableDynamicPayload(handle, 0, 1);
}

```

> Uwaga: `NRF24_EnableDynamicPayload()` samo zarządza bitem `EN_DPL`, więc powyższy zapis do `feature` przed jej wywołaniem jest nadmiarowy, ale nieszkodliwy — `EnableDynamicPayload` i tak nadpisze `FEATURE` po ustawieniu `DYNPD`. Można to uprościć, wywołując najpierw `NRF24_EnableDynamicPayload(handle, 0, 1)`, a dopiero potem ustawiając `EN_ACK_PAY`.

### Testy weryfikacyjne

- [ ] Po `NRF24_EnableAckPay(handle, 1)` rejestr `FEATURE` ma ustawione oba bity: `EN_ACK_PAY` i `EN_DPL`.
- [ ] Rejestr `DYNPD` ma ustawiony bit `DPL_P0`.
- [ ] Test transmisji ACK z payloadem między dwoma modułami — payload trafia poprawnie do PTX.
- [ ] `NRF24_EnableAckPay(handle, 0)` nie kasuje `EN_DPL`, jeśli DPL było używane niezależnie na innych pipe'ach (sprawdzić brak efektu ubocznego).

---

## 4. 🟠 Brak ochrony przed zapisem rejestrów w trybie aktywnym

### Problem

Wg Tabeli 20: *„W_REGISTER... Executable in power down or standby modes only.”* Żadna z funkcji konfiguracyjnych (`SetChannel`, `SetDataRate`, `SetPALevel`, `SetAutoAck`, `EnablePipe`, `SetRXAddress`, `SetTXAddress`, `SetPayloadSize`, `EnableDynamicPayload`, `SetAutoRetr`, `SetCRC`) nie sprawdza, czy urządzenie jest aktywne (`CE`=1, RX/TX mode), zanim wykona zapis. Zapis w trakcie aktywnego RX/TX jest niezgodny ze specyfikacją.

### Naprawa (wariant minimalny — zalecany na start)

Dodać w dokumentacji funkcji (komentarz Doxygen) jednoznaczne ostrzeżenie oraz dodać helper diagnostyczny, który aplikacja może wywołać przed rekonfiguracją:

```c
/**
 * @brief Sprawdza, czy urządzenie jest w trybie Power Down lub Standby
 *        (bezpiecznym do zapisu rejestrów konfiguracyjnych per datasheet).
 * @return 1 jeśli bezpiecznie, 0 jeśli aktywne RX/TX (CE=1).
 */
uint8_t NRF24_IsSafeToConfigure(NRF24_Handle_t *handle) {
    if (handle == NULL) return 0;
    return (HAL_GPIO_ReadPin(handle->ce_port, handle->ce_pin) == GPIO_PIN_RESET);
}

```

### Naprawa (wariant pełny — zalecany docelowo)

Wprowadzić prywatny helper `ensure_standby_for_write()` wywoływany na początku każdej z powyższych funkcji: jeśli `CE` jest wysoki, automatycznie ustawić `CE` nisko, wykonać zapis, a następnie (opcjonalnie) przywrócić poprzedni stan `CE` po stronie wywołującego kodu (wymaga zwrócenia flagi „czy trzeba przywrócić RX/TX” do aplikacji, albo świadomej decyzji, że rekonfiguracja zawsze wyprowadza urządzenie ze stanu aktywnego i wymaga ponownego wywołania `NRF24_SetMode()`).

Minimalna, bezpieczna wersja:

```c
static void ensure_standby(NRF24_Handle_t *handle) {
    ce_low(handle); // gwarantuje tryb Standby-I przed zapisem rejestru
}

```

i wywołanie `ensure_standby(handle);` na początku każdej z wymienionych funkcji, przed odczytem/zapisem rejestru.

> Decyzja projektowa: automatyczne opuszczanie `CE` w tle jest "ciche" i może zaskoczyć użytkownika API (traci aktywny RX bez wyraźnego wywołania). Zalecane rozwiązanie docelowe to jednak jawne udokumentowanie kontraktu: *„wywołaj* `NRF24_SetMode(STANDBY)` *przed jakąkolwiek rekonfiguracją”* + dodanie asercji/loga ostrzegawczego w trybie debug, zamiast cichej automatycznej zmiany stanu.

### Testy weryfikacyjne

- [ ] Rekonfiguracja kanału podczas aktywnego RX nie powoduje utraty pakietów w kolejnych testach integracyjnych.
- [ ] Dokumentacja API (nagłówki funkcji) zawiera jawne ostrzeżenie o wymogu trybu Standby/Power Down.

---

## 5. 🟡 Toggle `ACTIVATE` przy każdym `Init`

### Problem

```c
NRF24_Activate(handle); // wywoływane bezwarunkowo w NRF24_Init

```

Komenda `ACTIVATE` (0x50) nie występuje w tabeli komend nRF24L01+ w tym datasheecie — jest to hack kompatybilnościowy dla podróbek opartych na starszym silikonie nRF24L01. Ma naturę **toggle** (włącza/wyłącza naprzemiennie). Ponowne wywołanie `NRF24_Init()` (np. w procedurze odzyskiwania po błędzie) może **wyłączyć** wcześniej odblokowane funkcje FEATURE na chipach tego wymagających.

### Naprawa

Uczynić wywołanie idempotentnym — sprawdzić stan przed i po, i ponowić w razie potrzeby:

```c
HAL_StatusTypeDef NRF24_EnsureFeatureRegisterActive(NRF24_Handle_t *handle) {
    uint8_t feature_before, feature_after;

    // Test: spróbuj zapisać i odczytać EN_DYN_ACK w FEATURE.
    NRF24_ReadReg(handle, NRF24_REG_FEATURE, &feature_before);
    NRF24_WriteReg(handle, NRF24_REG_FEATURE, feature_before | NRF24_FEATURE_EN_DYN_ACK);
    NRF24_ReadReg(handle, NRF24_REG_FEATURE, &feature_after);

    if (!(feature_after & NRF24_FEATURE_EN_DYN_ACK)) {
        // Rejestr FEATURE zablokowany - wymagany ACTIVATE (klon starszego silikonu)
        NRF24_Activate(handle);
    }

    // Przywróć oryginalny stan FEATURE
    return NRF24_WriteReg(handle, NRF24_REG_FEATURE, feature_before);
}

```

i wywołać `NRF24_EnsureFeatureRegisterActive(handle)` zamiast bezwarunkowego `NRF24_Activate(handle)` w `NRF24_Init`.

### Testy weryfikacyjne

- [ ] Dwukrotne wywołanie `NRF24_Init()` pod rząd nie wyłącza funkcji DPL/ACK payload na module wymagającym ACTIVATE.
- [ ] Na oryginalnym nRF24L01+ zachowanie bez zmian (funkcja jest no-op / nieszkodliwa).

---

## 6. 🟡 Zbędne opóźnienie 1500µs w `NRF24_SetMode(STANDBY)`

### Problem

```c
case NRF24_MODE_STANDBY:
    config |= NRF24_CONFIG_PWR_UP;
    status = NRF24_WriteReg(handle, NRF24_REG_CONFIG, config);
    ...
    handle->delay_us(1500); // Tpd2stby max 1.5ms

```

`Tpd2stby` (1,5 ms) dotyczy wyłącznie przejścia **Power Down → Standby** (rozruch oscylatora krystalicznego). Funkcja czeka 1,5 ms za każdym razem, nawet gdy `PWR_UP` było już ustawione wcześniej (np. przejście RX→Standby→RX), niepotrzebnie wydłużając cykl pracy. `NRF24_PowerUp()` obok robi to poprawnie.

### Naprawa

```c
case NRF24_MODE_STANDBY:
    if (!(config & NRF24_CONFIG_PWR_UP)) {
        config |= NRF24_CONFIG_PWR_UP;
        status = NRF24_WriteReg(handle, NRF24_REG_CONFIG, config);
        if (status != HAL_OK) return status;
        ce_low(handle);
        handle->delay_us(1500); // Tpd2stby - tylko przy realnym Power Down -> Standby
    } else {
        ce_low(handle); // już zasilone - tylko opuść CE
    }
break;

```

### Testy weryfikacyjne

- [ ] Pomiar czasu przejścia RX→Standby→RX przed/po zmianie (oczekiwane skrócenie o ~1,5 ms na cykl).
- [ ] Przejście Power Down→Standby nadal poprawnie odczekuje 1,5 ms (brak regresji przy zimnym starcie).

---

## 7. 🟡 `MAX_RT` automatycznie kasuje TX FIFO

### Problem

```c
if (status & NRF24_STATUS_MAX_RT) {
    NRF24_FlushTX(handle);
}

```

Wg datasheeta (sekcja 7.8.7, Fig. 24) po przekroczeniu `ARC` payload w TX FIFO **pozostaje** i aplikacja może świadomie zdecydować o ręcznej retransmisji (impuls na `CE`) albo o porzuceniu pakietu. Bezwarunkowy `FlushTX` w handlerze IRQ odbiera tę możliwość i wymusza jedną, sztywną politykę (zawsze porzuć pakiet).

### Naprawa

Usunąć automatyczny `FlushTX` z handlera i pozostawić decyzję warstwie aplikacyjnej, np. przez callback lub kod powrotny:

```c
typedef enum {
    NRF24_IRQ_NONE     = 0,
    NRF24_IRQ_MAX_RT   = 0x01,
    NRF24_IRQ_TX_DS    = 0x02,
    NRF24_IRQ_RX_DR    = 0x04
} NRF24_IRQ_Event_t;

uint8_t NRF24_IRQ_Handler(NRF24_Handle_t *handle) {
    if (handle == NULL) return NRF24_IRQ_NONE;
    uint8_t status = NRF24_GetStatus(handle);
    uint8_t events = NRF24_IRQ_NONE;

    if (status & NRF24_STATUS_MAX_RT) {
        events |= NRF24_IRQ_MAX_RT;
        // Decyzja o FlushTX / retransmisji należy teraz do warstwy aplikacyjnej.
    }
    if (status & NRF24_STATUS_TX_DS) events |= NRF24_IRQ_TX_DS;
    if (status & NRF24_STATUS_RX_DR) events |= NRF24_IRQ_RX_DR;

    NRF24_ClearIRQ(handle, status & NRF24_STATUS_IRQ_MASK);
    return events;
}

```

Aplikacja, otrzymawszy `NRF24_IRQ_MAX_RT`, decyduje: `NRF24_FlushTX()` (porzuć) albo krótki impuls `CE` (ponów transmisję ostatniego pakietu z TX FIFO).

> To zmiana sygnatury publicznego API (`void` → `uint8_t`) — wymaga aktualizacji wszystkich miejsc wywołujących `NRF24_IRQ_Handler` w kodzie aplikacji (np. main.c, obsługa przerwania EXTI).

### Testy weryfikacyjne

- [ ] Scenariusz „utracony ACK, max retransmisji” — aplikacja poprawnie otrzymuje flagę `MAX_RT` i TX FIFO nie jest automatycznie czyszczony.
- [ ] Ręczna retransmisja (impuls CE) po `MAX_RT` skutecznie wysyła zalegający pakiet.
- [ ] Zaktualizowane wywołania `NRF24_IRQ_Handler` w kodzie aplikacyjnym kompilują się i działają zgodnie z nową sygnaturą.

---

## 8. 🟡 `NRF24_IsDataAvailable` — brak walidacji pipe i pełnego drenażu FIFO

### Problem

```c
uint8_t NRF24_IsDataAvailable(NRF24_Handle_t *handle, uint8_t *pipe) {
    ...
    if (status & NRF24_STATUS_RX_DR) {
        if (pipe) *pipe = (status >> 0x01) & 0x07;
        return 0x01;
    }
    return 0x00;
}

```

- Nie odrzuca wartości `6` (nieużywana) ani `7` (RX FIFO Empty) zwróconej w `RX_P_NO`.
- Opiera się wyłącznie na fladze `RX_DR`, a per przypis c (Tabela 28) po odczycie payloadu należy sprawdzać `FIFO_STATUS.RX_EMPTY` w pętli — kolejne pakiety (szczególnie przy MultiCeiver, do 6 nadajników) mogą czekać w FIFO bez ponownego ustawienia `RX_DR`.

### Naprawa

```c
uint8_t NRF24_IsDataAvailable(NRF24_Handle_t *handle, uint8_t *pipe) {
    if (handle == NULL || pipe == NULL) return 0;

    uint8_t status = NRF24_GetStatus(handle);
    uint8_t pipe_no = (status >> 1) & 0x07;

    // RX_DR ustawione ORAZ pipe_no w prawidłowym zakresie 0-5
    if ((status & NRF24_STATUS_RX_DR) && (pipe_no <= 5)) {
        *pipe = pipe_no;
        return 1;
    }

    // Zabezpieczenie dodatkowe: sprawdź też FIFO_STATUS, gdyby RX_DR nie zostało
    // ponownie ustawione, a w FIFO wciąż czekają dane (np. po poprzednim odczycie).
    uint8_t fifo_status = NRF24_GetFIFOStatus(handle);
    if (!(fifo_status & NRF24_FIFO_RX_EMPTY) && pipe_no <= 5) {
        *pipe = pipe_no;
        return 1;
    }

    return 0;
}

```

### Zalecenie wzorca użycia (do dokumentacji/README)

Po każdym przerwaniu `RX_DR` aplikacja powinna odczytywać payloady **w pętli**, aż `NRF24_GetFIFOStatus(handle) & NRF24_FIFO_RX_EMPTY` będzie prawdziwe — dopiero wtedy FIFO jest faktycznie opróżnione (istotne przy MultiCeiver, gdy kilka PTX nadaje niemal jednocześnie).

### Testy weryfikacyjne

- [ ] Test z dwoma/trzema nadajnikami PTX wysyłającymi jednocześnie do jednego PRX (MultiCeiver) — wszystkie pakiety zostają poprawnie odebrane i żaden nie „ginie” w FIFO.
- [ ] `pipe` nigdy nie przyjmuje wartości 6 lub 7 po wywołaniu funkcji zwracającej `1`.

---

## Harmonogram sugerowany


| Etap   | Zakres                                                                        | Szacowany nakład                                   |
| ------ | ----------------------------------------------------------------------------- | -------------------------------------------------- |
| Etap 1 | Punkty 1, 2 (krytyczne bugi, bez zmian API)                                   | 0,5 dnia + testy na sprzęcie                       |
| Etap 2 | Punkty 3, 8 (poprawki logiki Enhanced ShockBurst / MultiCeiver)               | 1 dzień + testy integracyjne z 2+ modułami         |
| Etap 3 | Punkty 5, 6 (optymalizacje, brak zmian API)                                   | 0,5 dnia                                           |
| Etap 4 | Punkty 4, 7 (zmiany kontraktu API — wymagają aktualizacji kodu aplikacyjnego) | 1 dzień + regresja całego projektu Weather-Station |


## Ogólne zalecenia uzupełniające (poza listą błędów)

- Dodać testy jednostkowe/HIL (hardware-in-the-loop) sprawdzające co najmniej: odbiór z 2 różnych pipe'ów jednocześnie, ACK z payloadem, utratę pakietu i retransmisję, przełączanie data rate/PA level w trakcie działania.
- Rozważyć dodanie w `NRF24_Handle_t` pola śledzącego bieżący tryb pracy (cache), aby uniknąć zbędnych odczytów `CONFIG` przed każdą zmianą trybu.
- Udokumentować w README repozytorium ograniczenie: biblioteka wspiera tylko tryb „jeden pakiet na impuls CE” w TX (brak trybu ciągłego opróżniania TX FIFO przy CE trzymanym wysoko).

