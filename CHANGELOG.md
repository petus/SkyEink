# Changelog

Formát podle [Keep a Changelog](https://keepachangelog.com/cs/1.1.0/), verzování podle [Semver](https://semver.org/lang/cs/).

## [0.1.0] - 2026-09-02

První vydání.

### Přidáno

**Přelety družic**
- Predikce viditelných přeletů na nejbližší noc ze zvolených skupin katalogu Celestrak.
- Filtr viditelnosti: družice osvětlená Sluncem, pozorovatel ve tmě, odhad vizuální magnitudy pod nastavenou mezí.
- Geometrický předfiltr podle roviny dráhy včetně uzlové precese. Objekt, který se nad stanoviště nedostane, se vůbec nepropaguje.
- Slučování přeletů, které vrcholí do tří minut od sebe ve stejném směru. Řetěz Starlinků se vypíše jako jedna položka s počtem.
- Filtr Starlinku podle středního pohybu, aplikovaný už během stahování.

**Počasí**
- Hodinová oblačnost, teplota a srážky z Open-Meteo pro okno tmy.
- Souhrn: do kdy je jasno, rozsah teplot, úhrn srážek a nejvyšší pravděpodobnost.

**Astronomie**
- Polohy Slunce, Měsíce a pěti planet počítané lokálně, bez sítě.
- Fáze Měsíce, časy soumraku a astronomické noci.

**Obrazovky**
- Noc: oblačnost, tabulka přeletů, pás obzoru s planetami a Měsícem.
- Den: nejbližší přelet, oblačnost, časy soumraku.
- Detail přeletu: polární graf s dráhou, odpočet, magnituda, směr.
- Stav: stáří dat, napětí a stav nabití, časové pásmo, volné místo.
- Přepínání Den a Noc podle výšky Slunce, bez zásahu uživatele.
- Trvalá stavová lišta se stářím dat na každé obrazovce.

**Napájení a probouzení**
- Deep sleep mezi obnovami, dělený po hodinových úsecích s korekcí času podle RTC.
- Událostní probuzení k přeletu: pět minut před vzestupem, v okamžiku vzestupu a minutu po zapadnutí.
- Tichý režim a strop počtu probuzení za noc.
- Snížení činnosti při nízkém napětí, zákaz zápisu do flash pod 3,3 V.

**Nastavení**
- Konfigurační přístupový bod se zabezpečeným heslem odvozeným z MAC adresy, QR kód na displeji.
- Volba jazyka, česky nebo anglicky, pro displej i konfigurační stránku.
- Souřadnice stanoviště, časové pásmo z předvolby nebo vlastní řetězec POSIX TZ.
- Výběr skupin katalogu, mez magnitudy, minimální elevace, nastavení probouzení.

**Čas**
- Tři zdroje v pořadí: NTP, hlavička `Date` z odpovědi HTTPS, čas z prohlížeče při konfiguraci.
- Přesný čas drží RTC PCF85063A se zálohovací baterií.

**Data a odolnost**
- Elementy se stahují ve formátu CSV a dvouřádkové prvky se skládají v zařízení, protože Celestrak pro nové objekty TLE neposkytuje.
- Cache elementů na microSD, se zálohou do LittleFS, když karta chybí.
- Nová cache nahradí starou až po úplném stažení. Při nedostatku místa nebo chybě zápisu zůstává v platnosti předchozí.
- Při výpadku sítě se zobrazí poslední data se stářím. Planety a Měsíc fungují dál.
- Stavy poruch: opakované selhání WiFi, elementy starší než 14 dní, plné úložiště, vybitá baterie.

**Přenositelnost**
- Veškerý hardware v jediném souboru `board_inkplate5v2.cpp`.
- Rozvržení počítané z rozměru panelu, žádné pevné souřadnice ve vykreslování.
- Sémantické odstíny místo číselných úrovní, s rastrem na jednobitovém panelu.
- Kontroly `validateNight()` a `validatePass()` hlídají překryvy a přetečení.

### Známá omezení
- Osm odstínů šedi a částečná obnova se na Inkplate vylučují.
- Panel má provozní rozsah 0 až 50 °C.
- Přerušení z PCF85063A na GPIO39 není využité, klasický ESP32 neumí u `ext1` režim „kterýkoli pin v nule".
- Bez OTA, firmware se nahrává přes USB.
- Napájecí LED na desce není řízená z GPIO a odebírá zhruba 0,4 mA.

[0.1.0]: https://github.com/petus/SkyEink/releases/tag/v0.1.0
