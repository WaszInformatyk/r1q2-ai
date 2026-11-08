# R1Q2 — port i modernizacja dla Linux Mint

Dokument roboczy projektu. Aktualizujemy go po każdym zakończonym etapie oraz po istotnych testach.

**Stan na:** 2026-11-06  
**Aktualny etap:** etapy 1–4 zakończone; kolejne prace to opcjonalne dopracowanie i nowe funkcje  
**Status:** cel osiągnięty — singleplayer, multiplayer, SDL2, audio i wszystkie trzy monitory do 4K potwierdzone przez użytkownika

## 1. Cel i sposób pracy

Celem jest stopniowe dostosowanie starego R1Q2/Quake II do współczesnego Linuxa bez jednoczesnego przepisywania silnika lub renderera.

Zasady:

- małe, odseparowane zmiany i test po każdym etapie;
- zachowanie działającego punktu odniesienia;
- szacunek dla istniejących hacków i punktów rozszerzenia: przed zmianą sprawdzamy ich rolę w konfiguracji i modach; w szczególności zachowujemy komendę/alias `newgame`;
- obecnie renderer OpenGL compatibility, bez migracji na core/shadery;
- aktywny backend to natywny SDL2; stary SDL 1.2 pozostaje tylko jako niekompilowany punkt odniesienia;
- audio SDL2 jest zaimplementowane i zaakceptowane; domyślne jest `s_initsound 1`, a wyłączenie pozostaje dostępne przez `s_initsound 0`;
- software renderer jest wyłączony w loaderze i menu;
- minimalna/domyślna rozdzielczość to 800x600;
- nie modyfikujemy oryginalnych danych gry ani katalogu `/home/user/.quake2` bez wyraźnej prośby użytkownika;
- nie wykonujemy commitów ani innych mutacji Git bez wyraźnej zgody użytkownika.

## 2. Środowisko

- Linux Mint 22 x86_64, jądro 6.8.0-147-generic;
- GCC 13.3.0, GNU Make 4.3;
- SDL 1.2.68 (warstwa zgodności nad SDL2), SDL2 2.30.0;
- libpng 1.6.43, libjpeg-turbo 2.1.5, zlib 1.3;
- testowy renderer: NVIDIA GeForce RTX 3060, OpenGL 4.6, sterownik 595.99.02;
- oryginalne zasoby gry: `/home/user/.quake2/baseq2` (nie są częścią repozytorium);
- opcjonalne, rekomendowane źródła referencyjne: `reference/quake2idsoftware/` — wyłącznie do porównań, nie do wspólnej kompilacji i nie do nadpisywania plików R1Q2.

## 3. Kompilacja i artefakty

Z katalogu głównego repozytorium:

```sh
make -C binaries -j"$(nproc)"
```

Pełna rekompilacja:

```sh
make -C binaries distclean
make -C binaries -j"$(nproc)"
```

Wyniki:

- `binaries/client/quake2`
- `binaries/r1q2ded/r1q2ded`
- `binaries/game/gamex86_64.so`
- `binaries/ref_gl/ref_gl.so`

Układ wymagany przy uruchomieniu z katalogu gry:

```text
quake2
ref_gl.so
baseq2/gamex86_64.so
baseq2/pak0.pak i pozostałe oryginalne zasoby
```

Renderer jest ładowany jako `./ref_gl.so`, dlatego katalog roboczy ma znaczenie.

## 4. Zakończony punkt startowy — SDL 1.2 / OpenGL

W pierwszej fazie wykonano:

- natywną kompilację wszystkich czterech komponentów dla x86_64;
- przejście z `libpng12` na systemowy libpng16 i libjpeg-turbo;
- aktualizację loadera PNG do publicznego API libpng, konwersję do RGBA i obsługę błędów;
- użycie systemowego `jpeg_mem_src` i poprawienie zwalniania pamięci JPEG;
- zmianę `image_t.texnum` na `GLuint`;
- domyślne `s_initsound 0`;
- wymuszenie `vid_ref gl` i usunięcie fallbacku `soft`/`softx`;
- ograniczenie menu wideo do OpenGL;
- domyślny tryb 800x600 i wymuszanie minimum 800x600 także dla starszych ustawień;
- automatyczne tworzenie katalogów `.depends`;
- dodanie `-lm` przy linkowaniu biblioteki gry;
- poprawienie definicji `cl_localents` i linkowania `SV_FixPlayerSkin` dla GCC 13;
- poprawienie testów bitowych przycisków myszy 4 i 5.

Testy punktu startowego:

- pełna rekompilacja zakończona powodzeniem;
- `r1q2ded +quit` zakończone kodem 0;
- klient z minimalnymi tymczasowymi zasobami uruchomił OpenGL i zakończył `+quit` kodem 0;
- osobno zweryfikowano ładowanie testowych PNG i JPEG;
- użytkownik potwierdził start z oryginalnymi zasobami, działanie `map q2dm1`, myszy i klawiatury oraz zmiany klasycznych rozdzielczości.

## 5. Etap 1 — poprawki przed SDL2

### 5.1. Start nowej gry singleplayer

W `client/menu.c` poprawiono sprawdzanie mapy z `CM_MapWillLoad("base1")` na `CM_MapWillLoad("maps/base1.bsp")`.

Ostatecznie zachowano sekwencję `loading ; killserver ; wait ; newgame`. Wcześniejsza tymczasowa zamiana na `map base1` została wycofana po wyjaśnieniu użytkownika: `newgame` może być definiowane w `.cfg`, wybierać właściwą mapę startową dla pełnej wersji/demo i pełnić rolę punktu rozszerzenia dla modów. Nie zastępujemy takich komend bez zrozumienia ich zastosowania.

Testy użytkownika potwierdziły uruchomienie `base1`, działanie aliasu `newgame` oraz przejścia `base1 -> base2 -> base3`. Kolejny test potwierdził również powrót `base3 -> base2` i save/load.

### 5.2. Błędne porównanie checksumy mapy na x86_64

Objaw zgłoszony dla `base1`:

```text
ERROR: Local map version differs from server: 0xcae30620 != 0xcae30620
```

Przyczyna: starszy serwer zapisuje checksumę przez `%i`. Dla wartości większych od `INT_MAX` powstaje ujemna liczba dziesiętna. `strtoul` na LP64 zwracał 64-bitową wartość ujemną modulo 2^64, która była porównywana bez rzutowania z 32-bitową checksumą mapy. Wypisanie przez `atoi` i `%x` pokazywało identyczne dolne 32 bity, maskując przyczynę.

W `client/cl_main.c` wynik `strtoul` jest teraz jawnie rzutowany do `uint32` przed porównaniem. Zachowuje to zgodność zarówno ze starymi ujemnymi, jak i nowymi dodatnimi zapisami checksum.

Test jednostkowy konwersji dla `0xcae30620` i starego zapisu `-891091424` zakończył się zgodnością 32-bitowych wartości.

### 5.3. Duże obrazy źródłowe PNG

Zgłoszony komunikat:

```text
Invalid or oversized PNG file: players/male/grunt.png
```

Loader akceptuje teraz źródłowe PNG do 4096x4096. Podczas uploadu OpenGL obrazy są nadal skalowane do dotychczasowego limitu `MAX_TEXTURE_DIMENSIONS` (1024), więc nie zmieniono jeszcze limitu tekstur GPU ani struktur renderera.

Testowy obraz RGBA 2048x2048 został poprawnie załadowany przez klienta; uruchomienie zakończyło się kodem 0 i bez komunikatu o nieprawidłowym rozmiarze. Właściwy `players/male/grunt.png` wymaga potwierdzenia przez użytkownika.

### 5.4. Inicjalizacja `avelocities`

W `CL_FlyParticles` i `CL_BfgParticles` zastąpiono indeksowanie poza wewnętrznym wymiarem tablicy dwoma zagnieżdżonymi pętlami. Zachowano kolejność losowania i logikę, usuwając niezdefiniowane zachowanie zgłaszane przez GCC 13.

### 5.5. Awaria przy powrocie na wcześniej odwiedzoną mapę

Objaw po przejściach `base1 -> base2 -> base3 -> base2`:

```text
Hunk_Free: munmap failed (22)
Error: recursive error after: Hunk_Free: munmap failed (22)
```

Przyczyna znajdowała się w cache rozmiarów modeli w `ref_gl/gl_model.c`, a nie w systemie zapisywania NPC:

- przy pierwszym ładowaniu modelu wywoływany był `Hunk_End()`, który zapisuje w nagłówku mmap ostateczny rozmiar obszaru;
- przy kolejnym ładowaniu wykorzystywano `model_size_cache` i pomijano `Hunk_End()`;
- nagłówek nowego hunka pozostawał zatem z wartością zero z `Hunk_Begin()`;
- podczas zwalniania modeli `Hunk_Free()` wykonywał `munmap(..., 0)`, co Linux odrzuca z `EINVAL` (22).

Poprawka wywołuje `Hunk_End()` również w gałęzi cache, zachowując dotychczasowy mechanizm `model_size_cache`. Nie zmieniono formatu savegame ani logiki odtwarzania stanu encji.

Test integracyjny na rzeczywistych funkcjach z `linux/q_shlinux.c` wykonał 100 cykli: pierwsze ładowanie, ładowanie z cache i zwolnienie hunka — wszystkie zakończyły się powodzeniem. Test smoke klienta/OpenGL również zakończył się kodem 0. Następnie użytkownik potwierdził pełną sekwencję `base3 -> base2` bez awarii oraz poprawne, także powtórne, wczytanie zapisu `02`.

Wypisywane wcześniej `Function offsets 0` pochodzi z diagnostyki `ReadLevel()` na systemach innych niż Windows i przy niezmienionym module gry nie jest komunikatem o błędzie.

## 6. Aktualne znane problemy

- przerwy w śladzie cząsteczek blastera — problem nadal występuje, ale ma niższy priorytet; algorytm celowo umieszcza cząsteczki co 5 jednostek;
- nowe rozdzielczości, custom, fullscreen desktop i wybór monitora zostały zaakceptowane przez użytkownika, także podczas gry multiplayer;
- audio SDL2 zostało zaakceptowane przez użytkownika na PulseAudio; domyślne jest `s_initsound 1`;
- pobieranie HTTP z serwerów nie jest zaimplementowane (`USE_CURL` wyłączone) — potencjalny osobny etap z libcurl;
- `couldn't exec r1gl.cfg` oznacza brak opcjonalnego pliku konfiguracyjnego i nie jest regresją;
- liczne stare ostrzeżenia kompilatora; usuwamy je selektywnie, nie masowo;
- etap 1 został potwierdzony: `newgame`, przejścia i powroty między mapami oraz save/load działają;
- natywny SDL2, pełny obrót myszy, konsola i Alt+Tab zostały potwierdzone na jednym monitorze;
- fullscreen desktop został potwierdzony na wszystkich trzech monitorach (1440x900, 1920x1080 i 3840x2160), także podczas gry multiplayer;
- joystick pozostaje wyłączony jak poprzednio; Unicode/IME, High DPI, fullscreen desktop i wybór monitora są celowo poza etapem 2.

## 7. Plan kolejnych etapów

### Etap 2 — natywny SDL2 zaimplementowany i potwierdzony w teście jednomonitorowym

Dodano `linux/gl_sdl2.c` i przełączono `binaries/ref_gl/Makefile` z `sdl-config` na `sdl2-config`. Stary `linux/gl_sdl.c` pozostaje w drzewie jako punkt odniesienia, ale nie jest kompilowany.

Zakres implementacji:

- `SDL_Window` i `SDL_GLContext` z jawnym OpenGL 2.1 compatibility profile;
- klasyczne rozdzielczości oraz fullscreen exclusive przez `SDL_SetWindowFullscreen` i `SDL_SetWindowDisplayMode`;
- zachowane `ref_gl.so`, QGL, `RW_IN_*`, `KBD_*`, `GetRefAPI` i `GetExtraAPI`;
- SDL2 keycodes/scancodes, key repeat, modyfikatory z licznikami oraz zwalnianie klawiszy przy utracie focusa;
- obsługa Mouse1–Mouse5, `SDL_MOUSEWHEEL`, relative mouse i `_windowed_mouse` bez zmiany znaczenia cvaru;
- kolejka zdarzeń z zachowaniem kolejności przy przepełnieniu;
- focus/minimize/close, Alt+Enter, Ctrl+G i reset starych delt myszy;
- VSync przez `SDL_GL_SetSwapInterval`, także aktualizowany w `GLimp_BeginFrame`;
- SDL2 window icon z poprawną paletą i kanałem alpha;
- symetryczne `SDL_InitSubSystem`/`SDL_QuitSubSystem(SDL_INIT_VIDEO)`, bez globalnego `SDL_Quit`;
- poprawne niszczenie kontekstu i okna przed `dlclose`, także przy błędach i restartach;
- joystick nadal wyłączony; bez zmian audio, cząsteczek, ABI i algorytmów renderera.

Celowo nie dodano: dynamicznych rozdzielczości, wyboru monitora, fullscreen desktop, High DPI, resize, Unicode/IME ani SDL_GameController.

Weryfikacja:

- pełna rekompilacja wszystkich komponentów po `distclean` zakończona powodzeniem;
- `ref_gl.so` linkuje `libSDL2-2.0.so.0`, nie zawiera zależności od SDL 1.2 ani symboli `SDL_WM_*`, `SDL_SetVideoMode`, `SDL_EnableKeyRepeat`, `SDL_GL_SwapBuffers`;
- wszystkie wymagane eksporty ABI są obecne;
- start OpenGL 800x600 zakończony kodem 0;
- start fullscreen exclusive zakończony kodem 0;
- trzy kolejne `vid_restart` zakończone kodem 0, z poprawnym ponownym utworzeniem kontekstu przy każdym ładowaniu;
- interaktywne wejście, Alt+Tab, Alt+Enter oraz oryginalne mapy wymagają testu użytkownika przed uznaniem etapu za w pełni zakończony.

### Etap 2.1 — mysz i wiele monitorów pod XFCE

Zgłoszenie użytkownika: w konfiguracji trzech monitorów gra startowała na głównym 1440x900, wszystkie ekrany na chwilę gasły, a obrót postaci kończył się na krawędzi ekranu. Problem występował także przy jednym monitorze.

Diagnoza:

- backend włączał `SDL_SetRelativeMouseMode` tylko przy `_windowed_mouse != 0`, którego domyślna wartość to `0`; mysz działała więc jak zwykły kursor ograniczony oknem;
- w `linux/vid_so.c` brakowało obecnej w źródłach id Software logiki `IN_Frame`, która przełącza wejście zależnie od `cl.refresh_prepped` i `cls.key_dest`;
- tryb bezramkowy nie jest wymagany do nieskończonego obrotu. Właściwym mechanizmem jest relative mouse podczas aktywnej rozgrywki;
- przygaszenie ekranów jest zgodne ze zmianą trybu przez fullscreen exclusive (`SDL_WINDOW_FULLSCREEN`), szczególnie pod wpływem XFCE/XRandR. Fullscreen desktop zostanie dodany w etapie 4 i powinien unikać takiej zmiany trybu.

Poprawki:

- przywrócono z referencyjnego `vid_so.c` aktywację wejścia tylko dla rozgrywki; menu/konsola zwalniają capture i pokazują kursor;
- fullscreen podczas rozgrywki zawsze używa `SDL_SetRelativeMouseMode(SDL_TRUE)` i `SDL_SetWindowGrab`;
- w trybie okienkowym capture jest kontrolowany przez `_windowed_mouse` (do pełnego obrotu w oknie ustawić `1`);
- `Ctrl+G` zachowuje możliwość ręcznego zwolnienia/przechwycenia myszy także w fullscreen;
- przy przełączeniach są zerowane stare delty, aby uniknąć skoku widoku.

Źródła wykorzystane przy diagnozie:

- dokumentacja SDL2 `SDL_SetRelativeMouseMode`: <https://wiki.libsdl.org/SDL2/SDL_SetRelativeMouseMode> — relative mode ukrywa kursor, ogranicza go do okna i raportuje ruch także na jego krawędzi;
- dokumentacja `SDL_SetWindowGrab`: <https://wiki.libsdl.org/SDL2/SDL_SetWindowGrab>;
- flagi `SDL_WINDOW_FULLSCREEN_DESKTOP`: <https://wiki.libsdl.org/SDL2/SDL_WindowFlags>;
- aktualny backend Yamagi Quake II `src/client/input/sdl2.c`: <https://github.com/yquake2/yquake2/blob/master/src/client/input/sdl2.c> — wykorzystuje zdarzenia relative motion oraz grab zależny od fullscreen/konfiguracji;
- zgłoszenie Yamagi/XFCE #586: <https://github.com/yquake2/yquake2/issues/586> oraz opis <https://discourse.libsdl.org/t/silly-xfce-feature-bug-breaks-relative-mouse-mode/28089> — znany problem XFCE `Wrap workspace when reaching the screen edge` może działać mimo gruba; diagnostycznym obejściem jest wyłączenie tej opcji albo `SDL_MOUSE_RELATIVE_MODE_WARP=1 ./quake2`. Nie włączamy warp mode globalnie, ponieważ zmienia sposób pozyskiwania ruchu i nie jest potrzebny do podstawowej poprawki.

Build i smoke fullscreen po poprawce przechodzą. Użytkownik potwierdził responsywną mysz, pełne sterowanie, zwalnianie kursora w konsoli i działanie Alt+Tab na jednym monitorze. Układ trzech monitorów pozostaje do przetestowania w etapie fullscreen desktop.

### Etap 3 — audio SDL2 zaimplementowane i zaakceptowane

Dodano `linux/snd_sdl2.c`, zastępując w buildzie stary OSS `linux/snd_linux.c`. Klient jest teraz kompilowany i linkowany również z `sdl2-config`.

Architektura:

- `SDL_OpenAudioDevice` z domyślnym urządzeniem systemowym;
- 16-bit stereo i częstotliwości z `s_khz`: 11025, 22050, 44100 lub 48000 Hz;
- współdzielony ring buffer o potędze dwójki, około 1–2 sekund, bez zmiany `s_mixahead`;
- callback SDL wyłącznie kopiuje próbki i przesuwa pozycję — nie wywołuje funkcji silnika;
- `SNDDMA_BeginPainting`/`Submit` używają `SDL_LockAudioDevice` z licznikiem zagnieżdżeń;
- `SNDDMA_GetDMAPos` zwraca pozycję w próbkach kanałowych zgodnie z `GetSoundtime`;
- `dma.samples`, `submission_chunk`, `samplebits` i jednostki ramek są zgodne z maskami miksera;
- urządzenie jest zamykane przed zwolnieniem ringu; shutdown jest idempotentny;
- tylko `SDL_InitSubSystem`/`SDL_QuitSubSystem(SDL_INIT_AUDIO)`, bez zamykania wideo ani globalnego `SDL_Quit`;
- błąd urządzenia nie blokuje dalszego uruchomienia gry.

W `client/snd_dma.c` przeniesiono liczniki wrapów do stanu globalnego i resetowano je przy inicjalizacji, aby `snd_restart` nie wracał do starej epoki czasu. Drzewo `knownsounds` jest ponownie używane przy wyłączonym/nieudanym audio i zerowane po zniszczeniu, co zapobiega wyciekom przy kolejnych inicjalizacjach.

Testy techniczne:

- start i wyjście z `SDL_AUDIODRIVER=dummy`: kod 0;
- wszystkie częstotliwości 11/22/44/48 kHz oraz `snd_restart`: kod 0 i poprawne parametry;
- kilka restartów dźwięku oraz `vid_restart` bez zamykania audio;
- testowy WAV 440 Hz odtworzony przez mikser; `soundinfo` pokazało postęp `samplepos` z 0 do 14336, a po kolejnym restarcie wideo do 17408;
- rzeczywiste otwarcie sterownika `pulseaudio` zakończone sukcesem;
- nieprawidłowy sterownik audio daje czytelny komunikat, ale klient nadal uruchamia OpenGL i wychodzi kodem 0;
- użytkownik potwierdził odsłuch oryginalnej gry na PulseAudio, wielokrotne `snd_restart` przy różnych częstotliwościach, `vid_restart`, wczytanie zapisu `02` i przejście `gamemap q2dm1`.

Po akceptacji odsłuchu domyślna wartość została zmieniona na `s_initsound 1`. Możliwość wyłączenia audio przez `s_initsound 0` została zachowana.

### Etap 4 — rozdzielczości i monitory zaimplementowane i zaakceptowane

Zmiany w backendzie i kliencie:

- `vid_fullscreen`: `0` — okno, `1` — fullscreen exclusive, `2` — fullscreen desktop bez zmiany trybu monitora;
- `vid_display` wybiera monitor SDL; numer jest zgodny z aktualną enumeracją systemu i może się zmienić po przełączeniu sprzętu;
- `gl_mode -1` używa custom `vid_width`/`vid_height`, a `gl_mode -2` aktualnej rozdzielczości wybranego monitora;
- presety 800x600 do 3840x2160, w tym 1280x720, 1366x768, 1600x900, 1920x1080, 2560x1440, 3440x1440 i 3840x2160;
- menu wideo zawiera wszystkie presety, `desktop`, `custom`, wybór fullscreen i listę monitorów z nazwą/rozdzielczością;
- pola custom width/height z zakresami 800–16384 i 600–16384;
- centrowanie na wybranym monitorze i ograniczenie okna do `SDL_GetDisplayUsableBounds`, aby nie wychodziło poza widoczny obszar;
- `SDL_WINDOW_ALLOW_HIGHDPI` i `SDL_GL_GetDrawableSize` synchronizują rzeczywisty rozmiar renderowania z `vid.width/height`;
- zmiana `vid_display`, custom width/height, trybu lub fullscreen powoduje kontrolowany restart wideo, a flagi modified są czyszczone po `R_SetMode`;
- Alt+Enter przełącza okno i fullscreen desktop;
- nie zmieniono `refimport_t` ani ABI modułu renderera.

Testy na rzeczywistym układzie XFCE/XRandR:

| `vid_display` | Monitor | Test | Wynik |
|---|---|---|---|
| 0 | DP-3, główny 1440x900 | custom windowed 1920x1080 | poprawnie ograniczone do widocznych 1440x860 |
| 0 | DP-3 | desktop fullscreen 1440x900 | kod 0, poprawny drawable |
| 0 | DP-3 | exclusive 1920x1080 | kod 0, poprawny drawable |
| 1 | HDMI-0 3840x2160 | desktop fullscreen 3840x2160 | kod 0, działa 4K |
| 1 | HDMI-0 | `vid_restart` 3840x2160 | kod 0, monitor i rozmiar zachowane |
| 2 | DP-0 1920x1080 | desktop fullscreen 1920x1080 | kod 0, poprawny wybór monitora |

Pełna rekompilacja po `distclean` zakończyła się powodzeniem. Użytkownik potwierdził zadowolenie z efektu końcowego oraz uruchomił grę multiplayer na serwerze zewnętrznym. Log potwierdza kolejno tryby 1440x900 na `vid_display 0`, 1920x1080 na `vid_display 2` i 3840x2160 na `vid_display 1`. Fullscreen exclusive nadal może powodować przygaszenie monitorów; do pracy wielomonitorowej rekomendowane jest `vid_fullscreen 2`.

### Etap 5 — cząsteczki, poprawność, optymalizacja

- porównanie cząsteczek z baseline sprzed SDL2;
- testy `cl_async`, point parameters/sprites i FPS;
- osobny build ASan/UBSan;
- priorytet dla rzeczywistych błędów pamięci, nie masowe czyszczenie ostrzeżeń;
- profilowanie przed optymalizacją;
- brak planu migracji na Vulkan/OpenGL core na tym etapie.

## 8. Checklist po każdym etapie

1. Pełna lub kontrolowana rekompilacja bez nowych błędów.
2. Start do menu z oryginalnymi zasobami.
3. `map q2dm1`, ruch, klawiatura, mysz i blaster.
4. `map base1` oraz start easy/medium z menu.
5. Save/load i przejście do kolejnej mapy po zmianach singleplayera.
6. Zmiana rozdzielczości, fullscreen/windowed i kilkukrotny `vid_restart`.
7. Alt+Tab, focus i przechwytywanie myszy po SDL2.
8. Wyjście, ponowny start i kontrola konfiguracji.
9. Aktualizacja tego dokumentu: zmiana, wynik testu, nowe problemy, kolejny krok.

## 9. Dziennik sesji

### 2026-11-06 — pierwszy działający build

- Naprawiono kompilację GCC 13/x86_64 i biblioteki obrazów.
- Uruchomiono SDL 1.2/OpenGL na NVIDIA.
- Ustawiono OpenGL jako jedyny renderer, minimum 800x600 i wyłączony dźwięk.
- Użytkownik potwierdził multiplayer `q2dm1` oraz działanie wejścia.

### 2026-11-06 — etap 1, baseline przed SDL2

- Dodano trwałą dokumentację i instrukcje kontynuacji dla kolejnych sesji AI.
- Naprawiono menu nowej gry, 64-bitowe porównanie checksumy i duże PNG.
- Usunięto niezdefiniowane indeksowanie `avelocities`.
- Pełna rekompilacja `distclean` + build przechodzi; test dużego PNG i konwersji checksumy przechodzi.
- Oczekiwany test użytkownika z oryginalnymi mapami i teksturami.

### 2026-11-06 — testy singleplayera i powrót na mapę

- Użytkownik potwierdził działanie `base1` oraz przejść `base1 -> base2 -> base3`.
- Przywrócono `newgame` jako konfigurowalny punkt rozszerzenia; pozostawiono poprawione sprawdzanie `maps/base1.bsp`.
- Zdiagnozowano pominięcie `Hunk_End()` w cache modeli jako przyczynę `munmap failed (22)` przy powrocie na mapę.
- Dodano finalizację hunka w gałęzi cache i przetestowano 100 cykli alokacji/zwalniania na rzeczywistym kodzie Linux.
- Pełna rekompilacja i test smoke klienta przechodzą; pełny test powrotu mapy oraz `newgame` oczekuje na użytkownika.

### 2026-11-06 — zakończenie etapu 1

- Użytkownik potwierdził `newgame` i uruchomienie `base1`.
- Sekwencja `base1 -> base2 -> base3 -> base2` działa bez błędu `munmap`.
- Zapisy `01` i `02` zostały utworzone; zapis `02` został poprawnie wczytany dwukrotnie.
- Etap 1 uznajemy za zakończony. Następny krok: natywny SDL2 bez zmiany funkcji.

### 2026-11-06 — implementacja natywnego SDL2

- Potwierdzono dostęp do źródeł referencyjnych id Software w `reference/quake2idsoftware/`.
- Dodano `linux/gl_sdl2.c`; stary SDL 1.2 pozostawiono jako niekompilowany baseline.
- Przeniesiono okno, kontekst GL, wejście, focus, fullscreen, ikonę i VSync na SDL2.
- Pełny build, start windowed/fullscreen i trzy restarty wideo przechodzą testy smoke.
- Oczekiwane testy użytkownika: klawiatura, mysz, Alt+Tab, Alt+Enter, `q2dm1`, `base1` i powrót na mapę.

### 2026-11-06 — XFCE, trzy monitory i ograniczony ruch myszy

- Zweryfikowano aktualną dokumentację SDL2 i implementację Yamagi Quake II oraz znany problem XFCE workspace wrapping.
- Zidentyfikowano brak aktywacji wejścia w `IN_Frame` i zbyt restrykcyjny warunek `_windowed_mouse` jako przyczyny ograniczonego obrotu.
- Przywrócono przełączanie capture zależnie od stanu gry; fullscreen używa relative mouse, menu/konsola pokazują kursor.
- Zachowano `_windowed_mouse` dla gry w oknie i działanie `Ctrl+G` jako ręcznego override.
- Build i smoke fullscreen przechodzą; oczekiwany test pełnego obrotu i zachowania trzech monitorów.

### 2026-11-06 — akceptacja SDL2 i implementacja audio

- Użytkownik potwierdził poprawną, responsywną mysz, zwalnianie kursora przez konsolę i Alt+Tab na jednym monitorze.
- Dodano backend `linux/snd_sdl2.c` i przeniesiono klienta na SDL2 audio, zachowując stary OSS tylko jako plik referencyjny.
- Zaimplementowano ring buffer, synchronizację callbacku, poprawne jednostki DMA, zamykanie urządzenia i reset wrapów przy `snd_restart`.
- Testy dummy obejmują start, cztery częstotliwości, restarty, odtworzenie WAV i postęp `samplepos`; PulseAudio otwiera się poprawnie.
- Oczekiwany odsłuch oryginalnej gry z `s_initsound 1`; po akceptacji zmienimy domyślną wartość i przejdziemy do dynamicznych trybów ekranu.

### 2026-11-06 — zakończenie etapu audio

- Użytkownik potwierdził zadowolenie z działania dźwięku na PulseAudio.
- Testy obejmują wiele restartów przy 11025/22050/44100 Hz, `vid_restart`, save/load i `gamemap q2dm1`.
- Domyślne `s_initsound` ustawiono na `1`; start na świeżej konfiguracji potwierdził inicjalizację audio bez dodatkowych parametrów, a `s_initsound 0` pozostaje dostępne jako fallback.
- Etap 3 zakończony. Następny krok: fullscreen desktop, dynamiczne rozdzielczości i wybór monitora.

### 2026-11-06 — implementacja rozdzielczości i monitorów

- Dodano fullscreen desktop, custom width/height, tryb natywny monitora, presety do 4K i wybór `vid_display`.
- Menu wideo wyświetla monitory SDL, nowe rozdzielczości i pola custom.
- Okno jest centrowane i ograniczane do usable bounds; drawable obsługuje High DPI.
- Testy potwierdziły wszystkie trzy monitory, desktop 4K, exclusive 1080p oraz restart wideo.
- Pełny build przechodzi. Oczekiwana interaktywna akceptacja menu i rozgrywki przed przejściem do cząsteczek/optymalizacji.

### 2026-11-06 — osiągnięcie celu i przekazanie projektu

- Użytkownik potwierdził zadowolenie z efektu końcowego i osiągnięcie celu.
- Gra połączyła się z zewnętrznym serwerem multiplayer; dźwięk PulseAudio działa podczas rozgrywki.
- W praktyce potwierdzono fullscreen desktop 1440x900, 1920x1080 i 3840x2160 na wszystkich trzech monitorach.
- Kolejny etap będzie dotyczył opcjonalnych detali i funkcji, nie ratowania podstawowej działalności projektu.
- Dodano `NEXT_SESSION_PROMPT.md` jako krótki punkt wejścia dla nowej sesji AI.

Nie wykonano commitów Git. Zmiany są zapisane w drzewie roboczym repozytorium.
