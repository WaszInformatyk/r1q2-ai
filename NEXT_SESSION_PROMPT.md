# Prompt startowy dla kolejnej sesji AI

Poniższy tekst można wkleić jako pierwszą wiadomość do nowej sesji. Plik `AGENTS.md` wskazuje również agentowi ten dokument i pełną historię w `PORTING_LINUX.md`.

---

Pracujesz z repozytorium `/home/user/git/r1q2-tsmod`, zawierającym stary kod R1Q2/Quake II. Odpowiadaj po polsku. Rozpocznij od przeczytania `AGENTS.md`, tego briefu i `PORTING_LINUX.md`, a następnie od sprawdzenia `git status --short` i aktualnych źródeł.

## Stan repozytorium — bardzo ważne

Wszystkie dotychczasowe poprawki znajdują się w drzewie roboczym i **nie zostały zacommitowane**. Nie wykonuj `git reset`, `git clean`, `git checkout --`, `git restore` ani innych operacji mogących usunąć lub nadpisać zmiany. Nie wykonuj commitów, pushów ani innych mutacji Git bez wyraźnej zgody użytkownika.

Nowe pliki, m.in. `linux/gl_sdl2.c`, `linux/snd_sdl2.c`, `AGENTS.md`, `PORTING_LINUX.md` i ten brief, są nieśledzone przez Git, tak jak dokumentacja i wygenerowane artefakty kompilacji. Obecność wielu plików `.o` i `.depends` w `git status` jest oczekiwana.

Oryginalne dane gry są w `/home/user/.quake2`. Nie modyfikuj ich ani nie instaluj tam plików bez wyraźnej prośby. Źródła referencyjne id Software są w `reference/quake2idsoftware/`; traktuj je wyłącznie jako materiał do porównań, nigdy jako kod do nadpisania R1Q2 lub dołączenia do buildu.

## Co zostało osiągnięte i przetestowane

Projekt ma już działający, natywny build na Linux Mint 22 x86_64 z GCC 13.3. Użytkownik potwierdził osiągnięcie celu: działa singleplayer, multiplayer, dźwięk i obsługa trzech monitorów do 4K.

1. **Build i podstawowe poprawki x86_64**
   - klient, serwer dedykowany, `gamex86_64.so` i `ref_gl.so` kompilują się;
   - naprawiono m.in. wielokrotne definicje, inline/linkowanie, zależności `.depends`, `GLuint` i błędy LP64;
   - zaktualizowano PNG do libpng16 i JPEG do systemowego libjpeg-turbo;
   - PNG akceptuje źródła do 4096x4096, a upload nadal skaluje je do historycznego limitu 1024.

2. **Singleplayer**
   - zachowano konfigurowalne `newgame`; nie zastępuj go komendą `map` — alias jest punktem rozszerzenia dla cfg/modów;
   - poprawiono sprawdzanie `maps/base1.bsp`;
   - naprawiono porównanie checksum map powyżej `INT_MAX` przez jawne rzutowanie do `uint32`;
   - naprawiono `Hunk_Free: munmap failed (22)` przy powrocie na mapę przez wywołanie `Hunk_End()` również w gałęzi cache modeli;
   - użytkownik potwierdził `base1`, przejścia `base1 -> base2 -> base3 -> base2` oraz save/load.

3. **SDL2 video i wejście**
   - aktywny backend to `linux/gl_sdl2.c`; `linux/gl_sdl.c` pozostaje tylko jako niekompilowany punkt odniesienia;
   - OpenGL compatibility, bez migracji na core/shadery;
   - działa klawiatura, repeat, Mouse1–Mouse5, kółko, focus, Alt+Tab, Alt+Enter, Ctrl+G i `vid_restart`;
   - `IN_Frame` w `linux/vid_so.c` steruje capture na podstawie `cl.refresh_prepped` i `cls.key_dest`;
   - fullscreen podczas gry używa relative mouse; menu i konsola zwalniają grab i pokazują kursor;
   - w oknie pełny capture wymaga `_windowed_mouse 1`.

4. **Audio SDL2**
   - aktywny backend to `linux/snd_sdl2.c`; stary OSS nie jest kompilowany;
   - 16-bit stereo, częstotliwości 11025/22050/44100/48000 Hz z `s_khz`;
   - ring buffer i callback SDL zsynchronizowany przez `SDL_LockAudioDevice`;
   - działa PulseAudio, `snd_restart`, `vid_restart`, save/load i przejścia map;
   - domyślne jest `s_initsound 1`; `s_initsound 0` pozostaje fallbackiem.

5. **Rozdzielczości i monitory**
   - `vid_fullscreen`: 0=okno, 1=exclusive, 2=fullscreen desktop;
   - `vid_display` wybiera monitor SDL;
   - `gl_mode -1` używa custom `vid_width`/`vid_height`;
   - `gl_mode -2` używa natywnej rozdzielczości monitora;
   - menu zawiera presety do 3840x2160, `desktop`, `custom`, pola wymiarów i listę monitorów;
   - okno jest centrowane i ograniczane do usable bounds; `SDL_GL_GetDrawableSize` obsługuje High DPI;
   - Alt+Enter przełącza okno/fullscreen desktop;
   - `refimport_t` i ABI renderera nie zostały zmienione.

Aktualny układ testowy XFCE/XRandR:

- `vid_display 0`: DP-3, główny 1440x900;
- `vid_display 1`: HDMI-0, 3840x2160;
- `vid_display 2`: DP-0, 1920x1080.

Indeksy mogą się zmienić po przełączeniu sprzętu. Fullscreen desktop na wszystkich trzech monitorach został potwierdzony w grze multiplayer. Exclusive nadal może przygasać ekrany; do pracy wielomonitorowej zalecane jest `vid_fullscreen 2`.

## Domyślne zachowanie i kompilacja

Domyślnie: SDL2, OpenGL jako jedyny renderer, minimum 800x600, `s_initsound 1`. Software renderer jest celowo wyłączony.

Build:

```sh
make -C binaries -j"$(nproc)"
```

Pełna rekompilacja:

```sh
make -C binaries distclean
make -C binaries -j"$(nproc)"
```

Artefakty:

- `binaries/client/quake2`
- `binaries/r1q2ded/r1q2ded`
- `binaries/game/gamex86_64.so`
- `binaries/ref_gl/ref_gl.so`

Typowe wdrożenie po zmianach klienta/renderera, wyłącznie na prośbę użytkownika lub jako instrukcja dla niego:

```sh
cp binaries/client/quake2 /home/user/.quake2/
cp binaries/ref_gl/ref_gl.so /home/user/.quake2/
cp binaries/game/gamex86_64.so /home/user/.quake2/baseq2/
```

## Zasady dalszej pracy

- Małe, odseparowane etapy i test po każdej zmianie. Nie mieszaj nowych funkcji z optymalizacją, migracją API czy masowym czyszczeniem ostrzeżeń.
- Szanuj stare hacki i punkty rozszerzenia. Najpierw zrozum ich rolę w cfg/modach; nie „naprawiaj” ich automatycznie.
- Zachowaj OpenGL compatibility. Nie przechodź na Vulkan/core/shadery bez osobnej decyzji użytkownika.
- Nie usuwaj starego SDL 1.2 ani OSS z drzewa jako punktów odniesienia; po prostu ich nie kompiluj.
- Nie włączaj masowo ostrzeżeń jako błędów. Naprawiaj potwierdzone problemy poprawności.
- Testy zastępcze wykonuj tylko w `/tmp/opencode`. Nie deklaruj pełnego działania gry bez testu z oryginalnymi zasobami.
- Po każdym etapie aktualizuj `PORTING_LINUX.md` i, jeśli zmieniają się zasady projektu, `AGENTS.md`.
- Proś użytkownika o konkretny test/log, kiedy jest potrzebny. Dotychczasowe raporty z sekwencją komend i fragmentem logu działają bardzo dobrze.

## Znane, niewymagające pilnej naprawy zachowania

- Ślad blastera nadal ma przerwy. `CL_BlasterTrail` celowo tworzy cząsteczki co 5 jednostek. To kandydat na osobną analizę porównawczą, nie automatyczną zmianę.
- `couldn't exec r1gl.cfg` oznacza brak opcjonalnego pliku konfiguracyjnego i nie jest błędem krytycznym.
- `Function offsets ...` jest diagnostyką `ReadLevel()` na systemach innych niż Windows. Może mieć znaczenie przy zgodności savegame między innymi binariami/ASLR, ale obecne save/load działa.
- Komunikat `HTTP downloading supported by server but this client was built without USE_CURL` wskazuje brak funkcji HTTP, nie regresję SDL2.
- Joystick pozostaje wyłączony. Unicode/IME, SDL_GameController i pełna obsługa urządzeń audio nie zostały zaimplementowane.
- Istnieją stare ostrzeżenia kompilatora i potencjalne miejsca do ASan/UBSan, ale nie należy robić masowej przebudowy ze względu na nie.

## Propozycje kolejnych, osobnych zadań

Na początku przedstaw użytkownikowi krótką listę i wspólnie wybierzcie jedno zadanie. Nie realizuj wszystkich naraz.

1. Analiza przerywanego śladu blastera: baseline, `cl_async`, point parameters/sprites, generowanie vs rysowanie cząsteczek.
2. Pobieranie HTTP przez libcurl jako osobny etap, z testami bez zmiany protokołu i zachowaniem fallbacku.
3. Dopracowanie HUD/UI dla 1440p/4K i skalowania interfejsu (`gl_hudscale` już istnieje).
4. Wybrane poprawki poprawności z ASan/UBSan oraz powtarzalne testy restartów i map.
5. Dodatkowe funkcje wejścia, joystick/controller, IME lub urządzeń audio — każde osobno.
6. Dalsze detale wideo: odświeżanie, fullscreen edge cases, przenoszenie okna między monitorami i testy XFCE.
7. Optymalizacja dopiero po profilowaniu; bez przepisywania renderera dla samej nowoczesności.

Przed zmianą kodu przedstaw krótki plan wybranego zadania. Zachowaj obecny działający stan jako baseline i nie kasuj go podczas refaktoryzacji.

---

## Skrócona wersja promptu, jeśli potrzebna

Kontynuuj pracę w `/home/user/git/r1q2-tsmod` na działającym porcie R1Q2 dla Linux Mint x86_64. Najpierw przeczytaj `AGENTS.md`, `NEXT_SESSION_PROMPT.md` i `PORTING_LINUX.md`. Wszystkie poprawki są niezacommitowane — nie używaj `git reset`, `git clean` ani operacji kasujących zmiany. SDL2 video/audio, singleplayer, multiplayer, save/load i trzy monitory do 4K są już potwierdzone. Zachowaj małe etapy, OpenGL compatibility, konfigurowalne `newgame`, oryginalne hacki i test po każdej zmianie. Nie modyfikuj `/home/user/.quake2` bez prośby, a `reference/quake2idsoftware/` traktuj tylko jako referencję. Na początku zaproponuj użytkownikowi jeden osobny etap: analiza śladu blastera, libcurl HTTP, HUD 4K, ASan/UBSan albo inna drobna funkcja. Nie rozpoczynaj dużej przebudowy ani masowego czyszczenia ostrzeżeń.
