# SkyEink

Přelety družic, oblačnost a polohy planet na ePaper displeji Inkplate 5 Gen2. Napájení z akumulátoru, obnova jen tehdy, kdy má co říct.

Rozhraní je česky nebo anglicky, přepíná se v nastavení.

Navrhl [chiptron.cz](https://chiptron.cz) a Claude AI.

## Funkce

- Předpověď viditelných přeletů na nejbližší noc z katalogu Celestrak.
- Hodinová oblačnost, teplota a srážky z Open-Meteo.
- Polohy Slunce, Měsíce a planet počítané lokálně, fungují i bez sítě.
- Automatické přepínání denní a noční obrazovky podle výšky Slunce.
- Probuzení pět minut před každým jasným přeletem, s odpočtem a dráhou po obloze.
- Výdrž na akumulátoru v řádu měsíců.

## Hardware

[Soldered Inkplate 5 Gen2](https://soldered.com/product/inkplate-5-v2/). ESP32-WROVER, panel 720x1280 s osmi odstíny šedi, RTC PCF85063A se zálohovací baterií, microSD, nabíječka LiPo.

| Signál | Pin |
|---|---|
| Tlačítko WAKE | GPIO36 |
| Přerušení RTC | GPIO39 |

Obojí je v RTC doméně. Probouzení používá `ext0` na tlačítku a časovač, protože klasický ESP32 neumí u `ext1` režim „kterýkoli pin v nule" a oba signály jsou aktivní v nule.

## Instalace

1. Nainstalujte knihovny: **InkplateLibrary**, **Sgp4** (Hopperpop), **ArduinoJson**.
2. Vyberte desku **Soldered Inkplate 5 V2**.
3. Vložte microSD kartu ve FAT32. Bez ní se cache uloží do LittleFS, což vyžaduje schéma oddílů s prostorem pro souborový systém.
4. Nahrajte firmware.
5. Po prvním startu naskočí konfigurační přístupový bod. Nastavte WiFi, jazyk a hlavně **souřadnice stanoviště**, pro přelety je potřeba přesnost na kilometry.

Fonty jsou přibalené v `SkyEink/Fonts/`. Knihovna Inkplate je má v kořenovém adresáři, kam Arduino IDE nedosáhne, takže `<Fonts/...>` by se nenašlo.

## Ovládání

Deska má jedno uživatelské tlačítko, rozlišuje se dobou držení.

| Akce | Chování |
|---|---|
| krátký stisk | přepočet s ukazatelem průběhu |
| podržení 5 s | konfigurační přístupový bod |

Přístupový bod se vypne po pěti minutách. Heslo se odvozuje z MAC adresy.

## Jak se hledají přelety

Do seznamu se dostane jen přelet, který splňuje všechny tři podmínky: družice je dost vysoko nad obzorem, je osvětlená Sluncem a pozorovatel je ve tmě, a odhad vizuální magnitudy je pod nastavenou mezí.

Výpočet má dva stupně, protože jedna propagace SGP4 stojí na ESP32 kolem tří milisekund a hrubá síla je mimo hru:

1. **Předfiltr podle roviny dráhy.** Z inklinace, uzlu a uzlové precese se spočítá, jestli objekt může nad stanoviště za danou noc doletět. Je to pár násobení místo tisíců propagací.
2. **`Sgp4::nextpass()`** pro to, co filtrem projde. Skáče po oběžných dobách a maximum hledá Brentovou metodou.

## Data

**Celestrak**, formát CSV. Od července 2026 mají nové objekty šestimístná katalogová čísla a TLE se pro ně neposkytuje, takže se dvouřádkové elementy skládají v zařízení. Celestrak vynucuje jedno stažení na jednu aktualizaci dat, firmware proto stahuje nejvýš jednou denně a na HTTP 403 si nechá stará data.

**Open-Meteo** pro oblačnost, teplotu a srážky. Bez klíče a bez registrace.

Slunce, Měsíc a planety se počítají lokálně.

### Starlink

Plná skupina má přes 9000 objektů. Filtr na střední pohyb nad 15,2 otáčky za den, aplikovaný už během stahování, z nich vybere čerstvé řetězy, které ještě letí nízko. Provozní Starlinky na 550 km mají magnitudu +5 až +7 a okem je stejně neuvidíte.

Skupina `last-30-days` obsahuje čerstvé starty a je mnohem menší, doporučuju ji nechat zapnutou i bez plného Starlinku.

## Spotřeba

| Položka | Denně |
|---|---|
| Ranní synchronizace a výpočet | ~0,9 mAh |
| Podvečerní obnova počasí | ~0,3 mAh |
| Událostní probuzení | ~1,0 mAh |
| Deep sleep při 18 µA | ~0,45 mAh |
| **Celkem** | **~2,7 mAh** |

Napájecí LED na desce je připojená napevno přes rezistor 1 kΩ, není řízená z GPIO a odebírá zhruba 0,4 mA, tedy 10 mAh denně. Kdo chce z akumulátoru vyždímat maximum, musí odpájet R35.

## Struktura

```
SkyEink/
  SkyEink.ino            stavový automat, plánovač probouzení
  hal.h                  rozhraní mezi aplikací a hardwarem
  board_inkplate5v2.cpp  jediný soubor, který zná hardware
  layout.h/.cpp          všechny souřadnice
  screens.h/.cpp         vykreslení obrazovek
  ui.h/.cpp              text, rastry, formátování
  i18n.h/.cpp            texty česky a anglicky, generované
  astro.h/.cpp           Slunce, Měsíc, planety
  gp.h/.cpp              elementy z Celestraku
  passes.h/.cpp          predikce přeletů
  weather.h/.cpp         oblačnost, teplota, srážky
  portal.h/.cpp          konfigurační přístupový bod
  config, runtime, power, store, log, version
  Fonts/                 čtyři velikosti FreeSans
```

Port na jiný panel znamená napsat nový `board_*.cpp` a přepnout define v `board_config.h`. Ve vykreslování nejsou pevné souřadnice ani odstíny, všechno se počítá z rozměru panelu a z hlaviček fontů.

## Nástroje

Skripty v kořeni projektu, spouštějí se ručně před commitem:

| Skript | K čemu |
|---|---|
| `tools_i18n.py` | generuje `i18n.h/.cpp` a měří texty proti šířce na displeji |
| `tools_textwidth.py` | změří libovolný řetězec podle skutečných dat fontů |
| `tools_collisions.py` | hledá proměnné, které kolidují s makry knihoven |
| `tools_braces.py` | kontroluje párování závorek |
| `tools/OveritSkyEink` | skeč, který ověří převod epochy v RTC |

## Na co si dát pozor

Poznámky pro toho, kdo bude kód upravovat. Všechny se projevily jako něco jiného, než čím byly.

**`getStreamPtr()` vrací surová data.** Při `Transfer-Encoding: chunked` jsou v proudu i rámce s délkou v hexu. Parser JSON přečte `3ba` jako číslo, vrátí `Ok` a dokument je prázdný. Dekódování umí `writeToStream()` a `getString()`.

**RTC převádí epochu přes `localtime()` a `mktime()`.** Round trip vyjde jen při shodné `TZ` při zápisu i čtení, a `configTime()` si `TZ` přepisuje. Viz třída `TzUtc`.

**Makra knihoven.** `sgp4unit.h` definuje `pi`, `Arduino.h` definuje `F`, `bit` a `abs`, InkplateLibrary šedesát dalších včetně `ON`, `OFF`, `DEC` a `nop`. Kontroluje `tools_collisions.py`.

**Dvě verze SdFat v InkplateLibrary.** Starší má `blocksPerCluster()`, novější `bytesPerCluster()`. Metodu vybírá dvojice přetížení.

**`getTextBounds()` vrací obálku pixelů, ne krok kurzoru.** Mezera nemá pixely, takže vrací nulu a text se slije. Šířka se počítá z `xAdvance`.

**Watchdog.** Výpočet přeletů běží desítky sekund. Bez krátkého přerušení se nedostane ke slovu idle task.

**`timegm()` a `strptime()` nejsou na ESP32 zaručené.** Proto vlastní `astro::tmToUtc()` a ruční rozbor hlavičky `Date`.

## Co používají knihovny

| Úloha | Knihovna |
|---|---|
| SGP4 a hledání přeletů | Sgp4 |
| Poloha Slunce pro magnitudu | `sat.sunAz`, `sat.sunEl` |
| JSON | ArduinoJson s filtrem |
| QR kód | `espressif/qrcode` z jádra |
| Kreslení a fonty | Adafruit GFX z InkplateLibrary |

Vlastní kód zůstal tam, kde knihovna neexistuje: astronomie Slunce, Měsíce a planet, skládání elementů z CSV, geometrický předfiltr, rastrování šedí, měření kroku kurzoru.

## Co chybí

- Skyplot GNSS jako druhá obrazovka.
- Port na ePaper 7,5" 800x480.
- Vlastní seznamy NORAD ID.

## Licence

MIT
