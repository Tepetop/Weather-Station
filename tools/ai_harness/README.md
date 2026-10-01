# Harness AI dla Weather-Station

Uruchamiasz go na własnym komputerze, w lokalnej kopii repozytorium.
Przekazuje modelowi zadanie i wykonuje pętlę: odczyt kodu → operacja modelu
→ wykonanie → wynik kompilacji/testów → poprawki. Korzysta z OpenAI Responses
API i standardowej biblioteki Python; nie wymaga instalowania pakietów pip.

## Wymagania

- Python 3.10 lub nowszy i Git.
- Do kompilacji: CMake co najmniej 3.22, Ninja, GNU Arm Embedded Toolchain
  (`arm-none-eabi-gcc`, `arm-none-eabi-g++`, Newlib z `nano.specs`) w PATH.
- Dostęp do `https://api.openai.com`, klucz API i model obsługujący Responses
  API z function calling, dostępny na Twoim koncie.

Klucz ustawiasz jako zmienną środowiskową. Nie wpisuj go do repozytorium.
Wywołania są rozliczane na koncie API; harness nie korzysta z sesji ChatGPT.
Do API trafia zadanie oraz fragmenty kodu/wyniki narzędzi odczytane przez agenta.
Nie umieszczaj sekretów w zadaniu ani w źródłach. Filtr nazw plików nie wykryje
każdego sekretu wpisanego bezpośrednio w kod.

## 1. Pobierz kod i sprawdź zależności

Po scaleniu PR do main:

```bash
git clone https://github.com/Tepetop/Weather-Station.git
cd Weather-Station
```

Przed scaleniem pobierz gałąź z harness-em:

```bash
git fetch origin feat/ai-harness
git switch --track origin/feat/ai-harness
```

Jeśli ta gałąź już istnieje lokalnie: `git switch feat/ai-harness`.
Na Windows używaj `python`, na Linux zwykle `python3`.

```bash
python tools/ai_harness/harness.py doctor
```

**Świeża kopia repo nie zawiera `OutdoorUnit/Drivers`**, ignorowanego przez
`OutdoorUnit/.gitignore`. Otwórz `OutdoorUnit/outdoor_unit_vscode.ioc` w
STM32CubeMX i wygeneruj HAL/CMSIS. Zrób to w kopii projektu, aby zachować
własne zmiany w Core, CMake, pinach i zegarach; przenieś brakujący katalog
Drivers do lokalnego OutdoorUnit. Harness nie regeneruje CubeMX sam.

`doctor` pokazuje również inne brakujące ścieżki z wygenerowanego CMake.
Pełna kompilacja wykryje dodatkowo błędy kompilatora/linkera i brak Newlib:

```bash
python tools/ai_harness/harness.py build
python tools/ai_harness/harness.py test
```

| Projekt | Preset | Wariant |
|---|---|---|
| IndoorUnit_newMCU | Debug | STM32F103CBT6, LCD, RTC, odbiornik i logger |
| OutdoorUnit | Debug | STM32F103C8T6, NODE_ID=1, BME280 |
| OutdoorUnit | Debug-node0 | STM32F103C8T6, NODE_ID=0, SI7021/BMP280/TSL2561 |

Harness używa istniejących presetów. CMake musi widzieć toolchain w PATH
terminala. Sama instalacja CubeIDE nie zawsze udostępnia jego narzędzia w PATH.

## 2. Ustaw klucz i model

Windows PowerShell — klucz bez zapisywania w historii poleceń:

```powershell
$apiKeyInput = Read-Host "OpenAI API key" -AsSecureString
$env:OPENAI_API_KEY = [System.Net.NetworkCredential]::new("", $apiKeyInput).Password
$env:OPENAI_MODEL = "IDENTYFIKATOR_MODELU_Z_TWOJEGO_KONTA"
```

Linux bash:

```bash
read -rsp 'OpenAI API key: ' weather_api_key
export OPENAI_API_KEY="$weather_api_key"
unset weather_api_key
export OPENAI_MODEL='IDENTYFIKATOR_MODELU_Z_TWOJEGO_KONTA'
```

Podmień identyfikator modelu. Możesz też podać `--model IDENTYFIKATOR`.
Zmienne obowiązują w bieżącym terminalu; harness nie odczytuje automatycznie `.env`.

## 3. Przeanalizuj kod

Wymagany jest czysty stan Git — zapisz wcześniej własne zmiany. Analiza
działa też bez toolchainu; próba kompilacji zwróci wtedy opis brakujących zależności.

```bash
python tools/ai_harness/harness.py run "Sprawdź obsługę błędów I2C w OutdoorUnit. Wskaż funkcje, ryzyka i propozycję poprawek."
```

Bez `--apply` agent nie ma narzędzi zapisujących kod.

## 4. Zleć edycję

```bash
git switch -c ai/tsl2561-i2c
python tools/ai_harness/harness.py run --apply "Przerób TSL2561 w OutdoorUnit na I2C z przerwaniami, bez DMA i HAL_Delay. Dostosuj wywołania i callbacki. Zachowaj obsługę innych czujników na wspólnej magistrali."
```

To przykład zadania, nie przeróbka już wykonana w sterowniku. Taką zmianę
należy sprawdzić na sprzęcie, zwłaszcza współdzielenie magistrali i callbacki.

`--apply` wymaga osobnej gałęzi. Przed edycją harness kompiluje bazowy kod i
uruchamia testy. Jeśli to nie przejdzie, zatrzymuje się przed wywołaniem modelu
i zapisem kodu. Po każdej edycji ponawia weryfikację i przekazuje wynik modelowi.
Agent może poprawiać błędy; sukces wymaga poprawnej weryfikacji i odpowiedzi końcowej.

Dłuższe zadanie możesz umieścić w pliku poza repozytorium:

```bash
python tools/ai_harness/harness.py run --apply --task-file ../zadanie.txt
```

## 5. Przejrzyj wynik

```bash
git status --short
git diff
```

Nowe pliki sprawdzisz w raporcie harnessu; po ich przejrzeniu dodaj konkretne
ścieżki przez `git add` i sprawdź `git diff --cached`. Sam wykonujesz commit/push.

Lokalny katalog `.ai-harness/runs/<czas-id>/` zawiera:

- `events.jsonl`: wyniki narzędzi, zużycie tokenów i kod zakończenia;
- `report.md`: odpowiedź modelu lub przyczynę przerwania;
- `changes.diff`: diff, skracany przy dużym wyniku; pełny diff sprawdzaj w Git.

Kod zakończenia: `0` — ukończona analiza/zweryfikowana edycja; `1` — błąd
środowiska/API/weryfikacji bazowej; `2` — limit/nieukończone zadanie; `130` — Ctrl+C.
Niepowodzenie pozostawia zmiany do przeglądu. Harness nie cofa ich ani nie robi
commitów. Przed następnym zadaniem zapisz albo świadomie usuń pozostawione zmiany.

## Zakres i ustawienia

`config.json` określa presety, katalogi zapisu, limit kroków, sumę tokenów
wejściowych, limit wyjścia na odpowiedź i czas polecenia. Limit wejścia jest
sprawdzany przed kolejnym żądaniem; ostatnia odpowiedź może go przekroczyć.
Nie jest to sztywny limit kosztu pieniężnego. Długie zadania dziel na etapy.
`instructions.md` zawiera zasady HAL, USER CODE, enumy/maski hex i sprawdzanie
wywołań oraz obu stron protokołu. Te pliki edytuje użytkownik, nie agent.

Zapis: tylko `.c`/`.h` w `Core/Inc` i `Core/Src` obu modułów. Istniejące pliki
zmienia przez jednoznaczną zamianę fragmentu; nowe tworzy bez nadpisywania.
Nowy `.c` może wymagać ręcznego dopisania do CMake, żeby uczestniczył w kompilacji.
CubeMX, linkery, biblioteki ST, PCB, harness i picoserver są chronione przed zapisem.
Picoserver można czytać; jego istniejące testy agregacji są uruchamiane na komputerze.

Agent nie ma dowolnej powłoki, usuwania plików, commit/push ani programowania MCU.
Polecenia to stałe wywołania Git, CMake i unittest. To ograniczenia narzędzi,
nie pełny sandbox: CMake i testy wykonują kod projektu. Używaj zaufanej kopii repo.
Kompilacja i testy hosta nie potwierdzają działania radia, LCD, czasów przerwań
ani poboru prądu na sprzęcie.

## Testy implementacji

Testy offline z atrapą API sprawdzają ścieżki, zapis, tryb analizy, CRLF, feedback
kompilacji i pętlę function calling. Nie wymagają klucza:

```bash
python -m unittest discover -s tools/ai_harness/tests -v
```

Dokumentacja API: [Function calling — OpenAI](https://developers.openai.com/api/docs/guides/function-calling).
