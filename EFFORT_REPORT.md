# R1Q2 na współczesnym Linuxie — raport prac i dane do szacunku nakładu pracy

**Data raportu:** 2026-11-11  
**Zakres:** analiza starego R1Q2/Quake II, naprawa buildu i błędów x86_64, SDL2 video/input/audio, singleplayer, multiplayer i obsługa trzech monitorów do 4K  
**Przeznaczenie:** podstawa do osobnej symulacji nakładu pracy w modelu sprzed AI (ok. 2016 r.)

> Ten dokument opisuje wykonany zakres i dostarcza danych do estymacji. Nie jest timesheetem ani pomiarem rzeczywistego czasu pracy. Podane na końcu przedziały są hipotezą kalibracyjną dla doświadczonego zespołu, a nie gwarancją harmonogramu.

## 1. Punkt wyjściowy

Repozytorium zawierało historyczny kod R1Q2 z częściową obsługą Linux x86/x86_64, ale bez realnej możliwości zbudowania kompletu na Linux Mint 22 z GCC 13.3.

Najważniejsze problemy stwierdzone podczas analizy:

- stary system budowania z zależnościami `.depends` nietworzonymi automatycznie;
- niedziałający na nowym GCC cel `combine` z `--combine`;
- wielokrotne definicje i semantyka starego inline;
- zależność od `libpng12` oraz bezpośredni dostęp do nieprzezroczystych już struktur libpng;
- konflikt własnego `jpeg_mem_src` z libjpeg-turbo;
- 64-bitowe niezgodności typów i konwersji, m.in. `unsigned long` vs `GLuint` oraz checksumy map;
- SDL 1.2, historyczne API okna i wejścia;
- dźwięk wyłącznie przez OSS `/dev/dsp`;
- brak aktywnego renderera software w drzewie przy domyślnym fallbacku `softx`/`soft`;
- statyczna lista starych rozdzielczości kończąca się na 1600x1200;
- błędy ujawniające się dopiero przy konkretnych mapach i powrotach do nich;
- liczne stare hacki i punkty rozszerzenia, których nie można było mechanicznie „poprawiać” bez analizy konfiguracji i modów.

Dostępne były również źródła referencyjne id Software w `reference/quake2idsoftware/`. Podczas prac wykorzystano dodatkowo dokumentację SDL2, aktualny kod Yamagi Quake II i zgłoszenia problemów SDL/XFCE.

## 2. Zakres ilościowy zmian

Na dzień raportu:

| Element | Zakres |
|---|---:|
| Zmodyfikowane istniejące pliki źródłowe/build | 18 |
| Zmiany w tych plikach wg `git diff --numstat` | 375 dodanych / 244 usunięte linie |
| Nowe produkcyjne pliki C | 2 |
| `linux/gl_sdl2.c` | 1133 linie |
| `linux/snd_sdl2.c` | 254 linie |
| Łącznie nowe linie backendów | 1387 |
| Dodane linie łącznie, łącznie z nowymi plikami | 1762 |
| Usunięte linie | 244 |
| Główne artefakty buildu | 4 |
| Dokumentacja `AGENTS.md`, `PORTING_LINUX.md`, `NEXT_SESSION_PROMPT.md` | 578 linii |

Uwagi interpretacyjne:

- nowe backendy korzystają z istniejącego kodu i kontraktów R1Q2, więc liczby linii nie oznaczają 1387 linii pracy koncepcyjnie „od zera”;
- liczba linii nie oddaje trudności diagnoz LP64, cache modeli, synchronizacji audio i zachowania XFCE;
- najbardziej pracochłonne były analiza, debugowanie i testy, a nie samo dopisywanie kodu.

Build produkuje:

- `binaries/client/quake2`,
- `binaries/r1q2ded/r1q2ded`,
- `binaries/game/gamex86_64.so`,
- `binaries/ref_gl/ref_gl.so`.

## 3. Wykonana analiza i poprawki buildu

### Zmiany

- automatyczne tworzenie katalogów `.depends`;
- przejście na `sdl2-config` i `pkg-config` dla obrazów;
- dodanie `-lm` dla modułu gry;
- pozostawienie historycznego `combine` poza normalnym buildem;
- poprawienie `cl_localents` na jedną definicję i deklaracje `extern`;
- zmiana `SV_FixPlayerSkin` na funkcję lokalną `static`, zgodną z nowym linkowaniem GCC;
- zachowanie kompilacji bez `-Werror`, z naprawianiem błędów selektywnie zamiast masową walką z ostrzeżeniami;
- naprawa testów bitowych przycisków myszy 4 i 5;
- wymuszenie OpenGL i usunięcie niedziałającego fallbacku software;
- ustawienie początkowo minimalnej/domyślnej rozdzielczości 800x600.

### Znaczenie

Uzyskano powtarzalny baseline, na którym można było rozdzielić błędy starego kodu od błędów wprowadzanych podczas modernizacji. To warunek bezpiecznych kolejnych etapów.

## 4. Modernizacja obsługi obrazów i typów x86_64

### PNG

- przejście z `-lpng12` na systemowy libpng16;
- zastąpienie bezpośredniego dostępu do pól `png_info` publicznymi getterami;
- zamiana usuniętego `png_set_gray_1_2_4_to_8` na aktualny odpowiednik;
- kontrola rozmiaru wejścia i błędów przez `png_jmpbuf`;
- sprawdzanie podpisu, wymiarów, kanałów i długości wierszy;
- konwersja różnych formatów źródłowych do RGBA;
- zwiększenie akceptowanego źródła do 4096x4096, przy zachowaniu historycznego limitu uploadu 1024;
- poprawna obsługa dużych obrazów przed ich późniejszym skalowaniem w rendererze.

### JPEG

- usunięcie konfliktu lokalnego `jpeg_mem_src` z biblioteką systemową;
- użycie implementacji libjpeg-turbo;
- ograniczenie niebezpiecznej ścieżki czterech komponentów, której bufor nie obsługiwał poprawnie;
- naprawa zwalniania danych pliku po udanym odczycie.

### Typy i poprawność

- `image_t.texnum` zmienione z `unsigned long` na `GLuint`;
- poprawiony format wypisywania identyfikatora;
- zagnieżdżone pętle inicjalizacji `avelocities` zamiast niezdefiniowanego indeksowania poza wewnętrzny wymiar tablicy.

### Testy

- osobne testy PNG i JPEG zakończone kodem 0;
- test RGBA 2048x2048 zakończony powodzeniem;
- oryginalne zasoby gry działają w multiplayer i singleplayer.

## 5. Singleplayer i zgodność save/load

### Menu nowej gry

- poprawiono test obecności mapy na `CM_MapWillLoad("maps/base1.bsp")`;
- zachowano `newgame`, ponieważ użytkownik wskazał jego rolę jako aliasu cfg wybierającego właściwą mapę dla pełnej wersji/demo i potencjalnych modów;
- wycofano wcześniejszą, zbyt uproszczoną zamianę na `map base1`.

### Checksumy map

Objaw:

```text
ERROR: Local map version differs from server: 0xcae30620 != 0xcae30620
```

Przyczyna:

- serwer serializował checksumę przez `%i`;
- wartości powyżej `INT_MAX` stawały się ujemnym tekstem dziesiętnym;
- na LP64 `strtoul` zwracał 64-bitową wartość ujemną modulo 2^64;
- porównanie z 32-bitową checksumą było fałszywe mimo identycznego wypisu dolnych 32 bitów.

Poprawka polega na jawnym rzutowaniu odczytu do `uint32`, z zachowaniem zgodności ze starymi ujemnymi i nowymi dodatnimi zapisami.

### Powrót na wcześniej odwiedzoną mapę

Objaw:

```text
Hunk_Free: munmap failed (22)
Error: recursive error after: Hunk_Free: munmap failed (22)
```

Analiza wykazała, że:

- pierwszy load modelu finalizuje hunk przez `Hunk_End()`;
- gałąź korzystająca z `model_size_cache` pomijała `Hunk_End()`;
- nagłówek mmap zachowywał rozmiar zero;
- `Hunk_Free()` wykonywał `munmap(..., 0)`, odrzucane z `EINVAL`.

Poprawka wywołuje `Hunk_End()` również w gałęzi cache, bez zmiany formatu savegame i logiki encji.

### Potwierdzone rezultaty

- `base1` uruchamia się;
- `base1 -> base2 -> base3 -> base2` działa;
- zapisy `01` i `02` działają;
- zapis `02` wczytano poprawnie wielokrotnie;
- wykonano 100 testowych cykli rzeczywistych funkcji hunk first-load/cache/free;
- zachowano konfigurowalny alias `newgame`.

## 6. Natywny backend SDL2 video i wejścia

Powstał nowy `linux/gl_sdl2.c`. Stary SDL 1.2 pozostał jedynie jako punkt odniesienia i nie jest kompilowany.

### Okno i OpenGL

- `SDL_Window` i `SDL_GLContext`;
- jawny OpenGL 2.1 compatibility profile;
- zachowanie fixed-function renderera, QGL, `ref_gl.so`, `refimport_t` i ABI;
- poprawna inicjalizacja atrybutów przed utworzeniem kontekstu;
- `SDL_GL_SwapWindow`;
- VSync przez `SDL_GL_SetSwapInterval`;
- ikona okna przez SDL2 surface/palette;
- poprawne zwalnianie kontekstu i okna przed `dlclose`;
- obsługa błędów i wielokrotnych restartów bez globalnego `SDL_Quit`.

### Klawiatura

- tłumaczenie SDL2 keycodes na kody Quake;
- stan według `SDL_Scancode`, bez indeksowania tablicy dużymi keycode’ami;
- zachowane key repeat;
- liczniki dla lewych/prawych modyfikatorów;
- poprawne keyup także przy utracie focusa;
- kolejka zdarzeń z zachowaniem kolejności przy przepełnieniu;
- Alt+Enter i Ctrl+G;
- nie dodawano połowicznej obsługi Unicode/IME, która powodowałaby duplikację lub konflikty kodów.

### Mysz

- Mouse1–Mouse5 z poprawnym mapowaniem SDL2 X1/X2;
- osobny `SDL_MOUSEWHEEL` zamiast przycisków 4/5 z SDL1;
- relative mouse i `SDL_SetWindowGrab`;
- reset starych delt przy capture/focus/restart;
- `_windowed_mouse` zachowane dla gry w oknie;
- Ctrl+G jako ręczny override również w fullscreen.

### Przywrócenie logiki aktywacji gry

Na podstawie referencyjnego `vid_so.c` przywrócono w `IN_Frame` przełączanie na podstawie:

- `cl.refresh_prepped`,
- `cls.key_dest` (`key_game`, `key_console`, `key_menu`).

Efekt:

- rozgrywka przechwytuje mysz;
- menu i konsola zwalniają capture i pokazują kursor;
- powrót do gry przywraca relative mouse;
- Alt+Tab nie zostawia „wciśniętych” klawiszy ani starej delty.

### Testy

- start windowed i fullscreen;
- trzy kolejne `vid_restart`;
- wymagane eksporty ABI obecne;
- brak symboli SDL1 i zależności od SDL 1.2;
- użytkownik potwierdził pełny obrót, responsywność, konsolę i Alt+Tab.

## 7. Backend audio SDL2

Powstał nowy `linux/snd_sdl2.c`, zastępując OSS w buildzie. Klient jest linkowany z SDL2 również ze względu na audio.

### Architektura

- `SDL_OpenAudioDevice` z domyślnym urządzeniem;
- 16-bit stereo;
- `s_khz`: 11025, 22050, 44100 i 48000 Hz;
- współdzielony ring buffer o potędze dwójki, około 1–2 sekund;
- callback wyłącznie kopiuje dane i przesuwa pozycję;
- brak wywołań silnika z wątku audio;
- `SDL_LockAudioDevice` z licznikiem zagnieżdżeń przy malowaniu;
- zgodność `dma.samples`, `samplepos`, `submission_chunk` i jednostek ramek z mikserem;
- urządzenie zamykane przed zwolnieniem bufora;
- idempotentny shutdown;
- tylko `SDL_InitSubSystem`/`SDL_QuitSubSystem(SDL_INIT_AUDIO)`;
- błąd urządzenia nie blokuje startu grafiki.

### Poprawki frontendu

- liczniki wrapów przeniesione do stanu globalnego i resetowane przy `SNDDMA_Init`;
- uniknięcie powrotu do starej epoki czasu po `snd_restart`;
- ponowne użycie `knownsounds` przy wyłączonym/nieudanym audio;
- zerowanie wskaźnika po `rbdestroy`, bez kolejnych wycieków przy restartach.

### Testy

- `SDL_AUDIODRIVER=dummy`: start i wyjście kodem 0;
- wszystkie cztery częstotliwości i `snd_restart`;
- testowy WAV 440 Hz z postępem `samplepos` z 0 do 14336;
- kolejny `vid_restart` z postępem do 17408 i bez zamykania audio;
- rzeczywiste otwarcie `pulseaudio` zakończone sukcesem;
- błędny sterownik daje komunikat, ale klient nadal działa;
- użytkownik zaakceptował odsłuch oryginalnej gry;
- domyślne `s_initsound 1`, z możliwością `s_initsound 0`.

## 8. Rozdzielczości i obsługa wielu monitorów

### Cvar’y i tryby

- `vid_fullscreen`:
  - `0` — okno,
  - `1` — fullscreen exclusive,
  - `2` — fullscreen desktop;
- `vid_display` — wybór monitora SDL;
- `gl_mode -1` — custom z `vid_width`/`vid_height`;
- `gl_mode -2` — natywna rozdzielczość wybranego monitora.

### Menu i rozdzielczości

- presety od 800x600 do 3840x2160;
- dodane m.in. 1280x720, 1366x768, 1600x900, 1920x1080, 2560x1440, 3440x1440 i 3840x2160;
- `desktop` i `custom`;
- pola custom width/height;
- lista monitorów z nazwą i rozdzielczością;
- wybór rodzaju fullscreen.

### Zachowanie okna

- centrowanie na wybranym monitorze;
- ograniczanie do `SDL_GetDisplayUsableBounds`, aby okno było widoczne;
- `SDL_WINDOW_ALLOW_HIGHDPI`;
- `SDL_GL_GetDrawableSize` synchronizuje rzeczywisty drawable z `vid.width/height`;
- kontrolowany restart po zmianie trybu/monitora/wymiarów;
- czyszczenie flag `modified`, bez pętli restartów;
- Alt+Enter przełącza okno/fullscreen desktop;
- `refimport_t` i ABI nie zostały zmienione.

### Testy na rzeczywistym układzie

| Monitor | Test | Wynik |
|---|---|---|
| DP-3 1440x900, główny | desktop 1440x900 | działa |
| DP-3 | exclusive 1920x1080 | działa |
| DP-3 | custom windowed 1920x1080 | ograniczone do widocznych 1440x860 |
| HDMI-0 3840x2160 | desktop 4K | działa |
| HDMI-0 | `vid_restart` 4K | monitor i rozmiar zachowane |
| DP-0 1920x1080 | desktop 1080p | działa |

Użytkownik dodatkowo potwierdził wszystkie trzy tryby podczas gry multiplayer na zewnętrznym serwerze.

## 9. Dokumentacja i sposób pracy

Powstały:

- `PORTING_LINUX.md` — szczegółowa historia analizy, zmian, testów i planów;
- `AGENTS.md` — zasady dla kolejnych agentów;
- `NEXT_SESSION_PROMPT.md` — pełny i skrócony brief dla nowej sesji;
- `EFFORT_REPORT.md` — niniejszy raport do analizy nakładu pracy.

Istotna zasada procesu: małe etapy z testami i bez mieszania migracji video, audio, display, cząsteczek i optymalizacji. Istniejące hacki były analizowane przed zmianą, jak pokazuje wycofanie zbyt pochopnej zamiany `newgame`.

## 10. Testy i dowody działania

Najważniejsze scenariusze zaliczone:

- pełne rekompilacje po `distclean` z GCC 13.3;
- start dedykowanego serwera;
- start klienta z minimalnymi zasobami syntetycznymi i z pełnymi oryginalnymi danymi;
- PNG i JPEG, także duży PNG 2048x2048;
- `map q2dm1` z myszą i klawiaturą;
- `map base1` i start przez `newgame`;
- przejścia i powroty map singleplayer;
- save/load, także powtarzany;
- multiplayer na zewnętrznym serwerze;
- PulseAudio i wszystkie częstotliwości;
- `snd_restart` i `vid_restart`;
- trzy monitory, w tym 4K i restart 4K;
- 100 cykli testu hunk/cache/free;
- kontrolowane zachowanie przy błędnym sterowniku audio.

## 11. Co celowo pozostało poza zakresem

- analiza przerywanego śladu blastera (algorytm już tworzy cząsteczki co 5 jednostek);
- pobieranie HTTP przez libcurl — log `without USE_CURL` to brak funkcji, nie regresja;
- Unicode/IME i pełne text input;
- joystick/SDL_GameController;
- wybór konkretnego urządzenia audio;
- migracja na OpenGL core, shadery lub Vulkan;
- masowe czyszczenie historycznych ostrzeżeń;
- ASan/UBSan i pełna optymalizacja po profilowaniu;
- pełna zgodność wszystkich modów i innych systemów operacyjnych;
- usunięcie wszystkich historycznych hacków;
- `r1gl.cfg`, którego brak jest tylko opcjonalnym komunikatem, nie błędem krytycznym.

## 12. Dane do symulacji pracy bez AI (ok. 2016 r.)

### Wymagane kompetencje

Realistyczny zespół potrzebowałby:

- programisty C z doświadczeniem w starym kodzie i ABI LP64;
- znajomości Makefile, GCC, linkerów i bibliotek współdzielonych;
- znajomości SDL2 video/input/audio oraz OpenGL compatibility;
- doświadczenia z pamięcią `mmap`, cache’ami i formatami savegame;
- wiedzy o synchronizacji callbacków audio i jednostkach próbek;
- dostępu do oryginalnych zasobów gry i sprzętu testowego z wieloma monitorami;
- czasu na czytanie kodu R1Q2, referencji id Software i dokumentacji SDL.

### Najważniejsze czynniki podnoszące nakład pracy

- błędy niewidoczne przy prostym starcie, ujawniające się dopiero na konkretnych mapach;
- komunikaty pozornie sprzeczne, jak identyczne checksumy;
- zachowanie zależne od cache i od wcześniejszej kolejności map;
- konieczność odróżniania zamierzonych hacków od rzeczywistych błędów;
- synchronizacja wątku audio z mikserem i resetami;
- różnice SDL1.2/SDL2 w kółku, X1/X2, key repeat, capture i fullscreen;
- zachowanie XFCE przy wielu monitorach i workspace wrapping;
- konieczność testowania z oryginalnymi zasobami i na fizycznym sprzęcie;
- starsze wersje SDL2 w 2016 r. mogły wymagać fallbacków dla części obecnie użytych API;
- brak AI do szybkiej analizy wariantów, wyszukiwania podobnych implementacji i generowania szkieletów backendów.

### Czynniki ograniczające nakład pracy

- dostępny oryginalny kod id Software i R1Q2;
- istniejący OpenGL compatibility — nie trzeba przepisywać renderera;
- SDL2 zamiast pisania własnych backendów X11/audio;
- małe, odseparowane poprawki zamiast nowego silnika;
- powtarzalne scenariusze map i testy smoke;
- zachowane ABI i kontrakty DMA/RW_IN/KBD/GetRefAPI.

## 13. Pakiety do estymacji (hipoteza kalibracyjna)

Poniższe przedziały zakładają doświadczonego programistę C/SDL z dostępem do referencji, oryginalnych zasobów i sprzętu testowego. Obejmują analizę, implementację, lokalne debugowanie, testy pakietu i notatki. Nie obejmują nauki Quake II od zera ani komercyjnej certyfikacji na wielu dystrybucjach.

| Pakiet | Szacunek w osobodniach |
|---|---:|
| Analiza legacy, build, zależności i baseline | 2–4 |
| PNG/JPEG i poprawki typów/x86_64 | 2–4 |
| Singleplayer, checksumy, hunk i save/load | 2–5 |
| SDL2 video, klawiatura, mysz, focus i restarty | 6–12 |
| SDL2 audio, DMA, synchronizacja i restarty | 4–7 |
| Rozdzielczości, menu, fullscreen desktop i monitory | 4–8 |
| Integracja, regresja, testy użytkownika i dokumentacja | 4–7 |
| **Suma orientacyjna** | **24–47 osobodni** |

Rekomendowany rezerwowy współczynnik dla nietypowych defektów legacy, problemów sprzętowych i ograniczeń starego SDL2: **20–30%**, szczególnie jeżeli zespół nie zna R1Q2.

### Możliwe warianty zespołu

- **Jeden doświadczony generalista:** około 5–10 tygodni pracy przy pełnym zaangażowaniu, zależnie od znajomości kodu i czasu debugowania.
- **Dwie osoby techniczne plus testy part-time:** podobny całkowity nakład lub nieco większy przez koordynację; około 5–8 tygodni kalendarzowych, jeśli obrazy/singleplayer i dokumentacja przebiegają równolegle.
- **Trzy role (engine, SDL/video-audio, tester):** możliwe skrócenie kalendarza do około 4–7 tygodni, ale nie proporcjonalne zmniejszenie liczby osobodni.

To nie są wyniki pomiaru rzeczywistego czasu AI. Są to przedziały do niezależnej dyskusji i kalibracji w osobnej symulacji.

### Ścieżka krytyczna

1. baseline i build;
2. naprawy x86_64 i krytycznych błędów singleplayera;
3. SDL2 video/input;
4. audio i display (częściowo równolegle po ustabilizowaniu podstaw SDL2);
5. testy integracyjne i akceptacja na oryginalnych zasobach/sprzęcie.

### Czego symulacja nie powinna wliczać automatycznie

- przepisywania renderera na shadery/Vulkan;
- pełnej obsługi wszystkich systemów i modów;
- komercyjnego wsparcia wielu dystrybucji;
- funkcji pozostałych poza zakresem (HTTP, IME, controller, optymalizacja całego silnika);
- kosztu zakupu licencji na dane gry, jeśli użytkownik już je posiada.

## 14. Konkluzja

Projekt nie był jedynie zmianą flag kompilacji. Obejmował diagnozę błędów zależnych od architektury i historii użytkowania, nowe backendy SDL2 video/input/audio, naprawę singleplayera i save/load oraz pełną obsługę współczesnych trybow wyświetlania.

Największy udział pracy koncepcyjnej stanowiły: zrozumienie starego kodu, zachowanie jego kontraktów i hacków, analiza przypadków brzegowych oraz testy integracyjne. Dlatego przy porównaniu z epoką przed AI należy szacować przede wszystkim czas doświadczonej analizy/debugowania, a nie jedynie czas napisania 1762 dodanych linii kodu.
