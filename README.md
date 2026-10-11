# SkyEink

Bateriový ePaper displej, který každý večer spočítá, které družice dnes v noci opravdu uvidíte. K tomu přidá oblačnost po hodinách a polohy planet.

Rozhraní je česky nebo anglicky, přepíná se v nastavení.

Vytvořeno chiptron.cz a Claude AI.

---

## Co to je

Že dnes v noci poletí ISS, se dá zjistit na Heavens-Above za deset sekund. Jenže to musíte vědět, že se máte podívat, a pak ještě zjistit, jestli nebude zataženo.

SkyEink to udělá sám. Visí na stěně, jednou denně se probudí, spočítá noc a zbytek času spí.

### Přelety družic

Do seznamu se dostane jen přelet, který jde opravdu vidět. Musí platit tři věci naráz:

- družice je výš než nastavené minimum,
- je osvětlená Sluncem, zatímco pozorovatel je už ve tmě,
- odhad jasnosti je pod nastavenou mezí.

Bez těchhle podmínek dostanete stovky položek a okem z nich uvidíte pět.

### Oblačnost, teplota, srážky

Nejjasnější přelet ISS je pod mrakem k ničemu, takže tenhle pás je úplně nahoře. Hodinová předpověď na celé okno tmy z Open-Meteo.

### Planety a Měsíc

Počítají se přímo v desce z Meeusových a Standishových algoritmů, žádné API. Když vypadne WiFi, tahle část obrazovky funguje dál.

### Co je na displeji

Obrazovka se přepíná sama podle výšky Slunce, nic nemačkáte.

**Noc.** Nahoře pás oblačnosti, devět hodinových sloupců. Mraky se plní shora dolů, protože odtud přicházejí. Pod každým sloupcem hodina, teplota a srážky v milimetrech s pravděpodobností. Uprostřed tabulka přeletů: čas, objekt, směr od kterého ke kterému, maximální výška, magnituda a trvání. Dole pás obzoru s planetami a kotouč Měsíce se skutečnou fází.

**Detail přeletu.** Pět minut před vzestupem se deska probudí a přepne na polární graf oblohy s vykreslenou dráhou, velký odpočet a údaje o přeletu. V okamžiku vzestupu se odpočet změní na „právě probíhá" a minutu po zapadnutí se displej sám vrátí na přehled.

**Den.** Nejbližší večerní přelet, oblačnost a časy soumraku.

**Stav.** Naskočí sama při poruše. Stáří dat, napětí a stav nabití, časové pásmo, volné místo na kartě.

Na každé obrazovce je dole lišta se stářím dat a časem další aktualizace, takže je hned vidět, když se data dlouho neobnovila.

Hodiny v záhlaví neběží. ePaper se překresluje jen při změně, takže ukazuje čas posledního překreslení.

---

## Co člověk potřebuje

| Položka | Poznámka |
|---|---|
| **Soldered Inkplate 5 Gen2** | ESP32-WROVER, panel 720x1280 s osmi odstíny šedi, RTC se zálohovací baterií, slot na microSD, nabíječka LiPo |
| **Akumulátor LiPo** | jednočlánkový, konektor JST-PH 2,0 mm, doporučuju 1200 mAh a víc |
| **microSD karta** | naformátovaná na FAT32, stačí nejmenší, kterou seženete |
| **USB-C kabel** | datový, ne jen nabíjecí |
| **WiFi 2,4 GHz** | ESP32 pásmo 5 GHz neumí |

Nic dopájet nemusíte. Deska má všechno na sobě.

Karta není povinná. Bez ní se data uloží do interní flash, ale s kartou je to bez starostí a nemusíte řešit schéma oddílů.

---

## Návod pro začátečníka

Žádné Arduino IDE, žádné knihovny, žádné kompilování. Stačí prohlížeč.

### 1. Stáhněte firmware

Na GitHubu v sekci **Releases** si stáhněte soubor **`skyeink.bin`**. Je to jeden soubor, nic se nerozbaluje.

### 2. Připojte desku

Zapojte USB-C kabel do desky a do počítače. Musí to být datový kabel. Nabíjecí kabel bez datových vodičů se tváří stejně, ale počítač desku neuvidí.

### 3. Otevřete flasher

Jděte na **[esp32flasher.chiptron.cz](https://esp32flasher.chiptron.cz)**.

Musíte použít **Chrome nebo Edge**. Firefox a Safari neumí Web Serial, tedy rozhraní, přes které prohlížeč mluví se sériovým portem.

### 4. Nahrajte

1. Vyberte stažený soubor `skyeink.bin`.
2. Klikněte na připojení. Prohlížeč zobrazí seznam sériových portů.
3. Vyberte port desky a potvrďte.
4. Spusťte flashování a počkejte, než doběhne.

**Když se v seznamu portů nic neobjeví:** Inkplate používá převodník CH340C. Na Windows k němu doinstalujte ovladač CH340, na macOS bývá potřeba taky. Linux ho má v jádře.

### 5. Nastavte

Po nahrání se deska restartuje a naskočí konfigurační přístupový bod. Na displeji uvidíte jméno sítě, heslo a QR kód.

1. Připojte se telefonem k té síti. Načtením QR kódu to jde rychleji.
2. Otevřete v prohlížeči **http://192.168.4.1**.
3. Vyplňte jazyk, WiFi a hlavně **souřadnice stanoviště**.
4. Uložte. Deska se restartuje a začne stahovat data.

Stránka má tři záložky: základní nastavení, přelety a tabulku jasnosti. V tabulce si upravíte, jakou magnitudu firmware přiřadí jednotlivým typům objektů. U každé položky je krátká nápověda česky i anglicky. Nastavení se ukládá do flash a po resetu zůstává.

Přístupový bod se sám vypne po pěti minutách. Heslo se odvozuje z MAC adresy, takže je pro každý kus jiné a po restartu se nemění.

**Na souřadnice si dejte pozor.** Družice na nízké dráze letí sedm a půl kilometru za sekundu. Padesát kilometrů vedle znamená jiný čas i jiný směr. Zeměpisnou šířku a délku najdete třeba na mapy.cz pravým tlačítkem.

První výpočet trvá pár minut, protože se stahuje celý katalog. Další dny už jen doplňuje.

### Ovládání

Deska má jedno uživatelské tlačítko, rozlišuje se dobou držení.

| Akce | Co se stane |
|---|---|
| krátký stisk | přepočet, na displeji běží ukazatel průběhu |
| podržení 5 sekund | konfigurační přístupový bod |

---

## Pro pokročilého uživatele

Pokud chcete firmware upravovat nebo si ho jen přeložit sami.

### Arduino IDE

Testováno na Arduino IDE 2.x.

**1. Přidejte desku.** V *Soubor → Vlastnosti* vložte do pole *Správce dalších desek* URL Soldered:

```
https://raw.githubusercontent.com/SolderedElectronics/Dasduino-Board-Definitions-for-Arduino-IDE/master/package_Dasduino_Boards_index.json
```

Pak ve *Správci desek* najděte **Soldered Inkplate Boards** a nainstalujte.

**2. Vyberte desku.** *Nástroje → Deska → Soldered Inkplate Boards → Soldered Inkplate 5 V2*.

**3. Schéma oddílů.** Výchozí nastavení stačí. Pokud používáte zálohu do LittleFS místo SD karty, zvolte schéma, které má oddíl pro souborový systém.

### Knihovny

Nainstalujte přes *Nástroje → Spravovat knihovny*:

| Knihovna | Autor | Licence |
|---|---|---|
| **InkplateLibrary** | Soldered | LGPL-3.0 |
| **Sgp4** | Hopperpop | MIT |
| **ArduinoJson** | Benoit Blanchon | MIT |

InkplateLibrary v sobě nese Adafruit GFX (BSD) a SdFat (MIT), takže je instalovat zvlášť nemusíte.

QR kód generuje komponenta `espressif/qrcode`, která je součástí jádra ESP32. Neinstaluje se.

**Fonty jsou přibalené** v `SkyEink/Fonts/`. InkplateLibrary je má ve svém kořenovém adresáři, kam Arduino IDE při hledání hlaviček nesahá, takže zápis `<Fonts/FreeSans9pt7b.h>` by se nenašel. Kopie ve skeči je stejné řešení, jaké používají oficiální příklady Soldered.

### Struktura projektu

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
  gp.h/.cpp              orbitální elementy z Celestraku
  passes.h/.cpp          predikce přeletů
  weather.h/.cpp         oblačnost, teplota, srážky
  portal.h/.cpp          konfigurační přístupový bod
  config, runtime, store, log, version
  Fonts/                 čtyři velikosti FreeSans
```

Hardware je oddělený schválně a důsledně. **Žádný soubor kromě `board_inkplate5v2.cpp` neincluduje hlavičku od desky.** Port na jiný panel znamená napsat další takový soubor a přepnout define v `board_config.h`.

Drží to na dvou věcech. Odstíny jsou sémantické, aplikace kreslí `INK_MID`, ne číslo úrovně. A rozvržení se počítá z rozměru panelu, takže ve vykreslování není jediná souřadnice v pixelech.

### Jak se hledají přelety

Katalog má stovky objektů a jedna propagace SGP4 stojí na ESP32 kolem tří milisekund. Hrubá síla je mimo hru, takže výpočet má dva stupně.

**Geometrický předfiltr.** Z roviny dráhy, uzlu a uzlové precese se spočítá, jestli objekt může nad stanoviště za danou noc vůbec doletět. Je to pár násobení místo tisíců propagací. Hubbleův dalekohled má sklon dráhy 28,5 stupně a z padesáté rovnoběžky se nad dvacet stupňů nikdy nedostane, takže vypadne natrvalo.

**Vyhledávač z knihovny.** Na to, co filtrem projde, se pustí `Sgp4::nextpass()`. Skáče po oběžných dobách a maximum elevace hledá Brentovou metodou.

**Oříznutí na viditelnou část.** Přelet se projde po krocích a zůstane z něj jen úsek, kdy je družice osvětlená a Slunce je u pozorovatele aspoň 6° pod obzorem (občanský soumrak). Magnituda se počítá v nejvyšším bodě tohoto úseku. Družice, která vrcholí ve stínu Země, by jinak dostala jasnost, kterou nikdy neuvidíte.

Výsledek jsem porovnal s Heavens-Above. Večerní přelet ISS: SkyEink 19:39, maximum 65°, magnituda -3,6. Heavens-Above 19:41, 65°, -3,5.

**Cache.** Elementy i spočítané přelety se ukládají na kartu. Přelety mají otisk nastavení, takže se přepočítají jen po stažení nových elementů nebo po změně nastavení. Probuzení kvůli počasí nebo přeletu trvá kolem 16 sekund.

### Nástroje pro vývoj

**Pokud kód jen překládáte a nahráváte, tahle část se vás netýká.** Skripty
nejsou součástí sestavení, Arduino IDE je nespouští a nic bez nich nechybí.

Jsou pro toho, kdo do kódu sahá. Vyžadují Python 3 a spouštějí se ručně
z kořene projektu.

| Skript | K čemu | Kdy ho spustit |
|---|---|---|
| `tools_i18n.py` | **generuje** `i18n.h` a `i18n.cpp` z tabulky textů a rovnou měří, jestli se české i anglické znění vejde na displej | vždy po změně jakéhokoli textu, jinak se změna nikam nepropíše |
| `tools_collisions.py` | hledá proměnné, které se jmenují stejně jako makro některé knihovny | po přidání nových proměnných |
| `tools_braces.py` | kontroluje párování závorek, ignoruje komentáře a řetězce | po větší úpravě |
| `tools_textwidth.py` | změří libovolný řetězec podle skutečných dat fontů | když přidáváte text na displej |
| `tools/OveritSkyEink` | samostatný skeč, který ověří převod epochy v RTC | při podezření na posunutý čas |

`tools_i18n.py` je jediný, který soubory mění. Ostatní jen kontrolují a nic
nepřepisují.

### Na co si dát pozor při úpravách

Poznámky pro toho, kdo bude v kódu něco měnit. Všechny tyhle věci se projevily jako něco jiného, než čím byly.

**`getStreamPtr()` vrací surová data.** Při `Transfer-Encoding: chunked` jsou v proudu i rámce s délkou v hexu. Parser JSON přečte `3ba` jako číslo, vrátí `Ok` a dokument je prázdný. Dekódování umí jen `writeToStream()` a `getString()`.

**RTC převádí epochu přes `localtime()` a `mktime()`.** Round trip vyjde jen tehdy, když je `TZ` při zápisu i čtení stejná, a `configTime()` si `TZ` přepisuje. Řeší to třída `TzUtc`.

**Makra knihoven.** `sgp4unit.h` definuje `pi`, `Arduino.h` definuje `F`, `bit` a `abs`, InkplateLibrary šedesát dalších včetně `ON`, `OFF`, `DEC` a `nop`. Proměnná se stejným jménem se tiše rozvine a překlad spadne na místě, které s příčinou nesouvisí.

**Dvě verze SdFat v InkplateLibrary.** Starší má `blocksPerCluster()`, novější `bytesPerCluster()`. Metodu vybírá dvojice přetížení.

**`getTextBounds()` vrací obálku pixelů, ne krok kurzoru.** Mezera nemá žádné pixely, takže vrací nulu a text se slije dohromady. Šířka se počítá z `xAdvance`.

**Watchdog.** Výpočet přeletů běží desítky sekund. Bez krátkého přerušení se nedostane ke slovu idle task a deska se restartuje.

**`timegm()` a `strptime()` nejsou na ESP32 zaručené.** Proto vlastní `astro::tmToUtc()` a ruční rozbor hlavičky `Date`.

---

## Data a jejich omezení

**Celestrak** dodává orbitální elementy. Od března 2026 vynucuje jedno stažení na jednu aktualizaci dat, druhý dotaz vrátí 403 a opakované pokusy vedou k zablokování IP. Firmware proto stahuje nejvýš jednou denně a po 403 si nechá stará data.

V červenci 2026 došla pětimístná katalogová čísla. Nové objekty mají šestimístná a pro ta Celestrak formát TLE neposkytuje, takže se stahuje CSV a dvouřádkové elementy se skládají přímo v desce.

**Open-Meteo** dodává počasí. Bez klíče a bez registrace, data jsou pod licencí CC BY 4.0 a zdarma pro nekomerční použití. U čehokoli publikovaného je potřeba uvést zdroj.

**Čas** se shání třemi cestami v tomto pořadí: NTP, hlavička `Date` z odpovědi HTTPS a nakonec čas z prohlížeče při konfiguraci. Ta poslední nepotřebuje internet vůbec. Někteří poskytovatelé blokují UDP port 123, na kterém jede NTP, a pak nastoupí druhá cesta.

**Starlink** má přes 9000 objektů, tedy dva megabajty ke stažení a přes hodinu počítání. Filtr běží už během stahování a vybírá jen dráhy pod zhruba 500 kilometry, tedy čerstvé řetězy, které ještě stoupají a jsou jasné. Provozní Starlinky na 550 km mají magnitudu +5 až +7 a okem je stejně neuvidíte.

---

## Spotřeba

| Položka | Denně |
|---|---|
| Firmware (synchronizace, výpočet, probuzení k přeletům, deep sleep) | ~4 mAh |
| Napájecí LED na desce | ~10 mAh |

Napájecí LED na desce je připojená napevno přes rezistor 1 kΩ, není řízená z GPIO a odebírá kolem 0,4 mA. To je víc než dvojnásobek celého firmwaru. Kdo chce z akumulátoru vyždímat maximum, musí odpájet R35.

---

## Výhody

**Dlouhá výdrž.** Firmware sám spotřebuje zhruba 4 mAh denně. S akumulátorem 2000 mAh to jsou měsíce na jedno nabití, s odpájenou napájecí LED ještě víc. ePaper drží obraz i bez napájení, takže se překresluje jen tehdy, když se něco změní.

**Open source hardware i software.** Inkplate má veřejné schéma i podklady pro desku, firmware je pod MIT licencí. Obojí si můžete prohlédnout a upravit.

**Snadné úpravy.** Hardware je oddělený do jediného souboru, texty jsou v generované tabulce a rozvržení se počítá z rozměru panelu. Další jazyk, port na jiný ePaper nebo nová obrazovka znamená úpravu jednoho souboru.

---

## Omezení

- Osm odstínů šedi a částečná obnova se na Inkplate vylučují, takže hlavní obrazovky jedou plnou obnovou.
- Panel má provozní rozsah 0 až 50 °C. Na parapet v zimě to není.
- Bez OTA. Aktualizace se nahrává přes USB.
- Přerušení z RTC na GPIO39 není využité. Klasický ESP32 neumí u `ext1` režim „kterýkoli pin v nule" a oba signály jsou aktivní v nule.
- Verze 0.1.0. Funguje, ale je čerstvá.

---

## Co chybí

- Skyplot GNSS jako druhá obrazovka. Družice Galileo a GPS se po obloze posouvají tak pomalu, že by půlhodinová obnova dávala smysl.
- Port na ePaper 7,5" 800x480.
- Vlastní seznamy NORAD ID.

---

## Licence

Firmware je pod **MIT** licencí.

InkplateLibrary je LGPL-3.0, což dovoluje použití z jinak licencovaného programu za podmínky, že si uživatel může knihovnu vyměnit a přelinkovat. Zveřejněním celého zdrojáku je to splněné.

Fonty v `Fonts/` pocházejí z Adafruit GFX a jsou odvozené z GNU FreeFont, tedy GPL s výjimkou pro fonty. Ta výjimka výslovně dovoluje vložit font do programu, aniž by se program stal GPL.

---

Vytvořeno **chiptron.cz** a **Claude AI**.
