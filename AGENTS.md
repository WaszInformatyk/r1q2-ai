# Instrukcje dla agentów — R1Q2 Linux

Przed rozpoczęciem pracy przeczytaj `NEXT_SESSION_PROMPT.md` oraz `PORTING_LINUX.md`. Pierwszy plik jest krótkim briefem dla nowej sesji, a drugi zawiera pełną historię zmian, wyniki testów, znane problemy i plan kolejnych etapów. Po każdym etapie zaktualizuj dokumentację.

## Projekt

- Stary kod C R1Q2/Quake II, budowany natywnie na Linux Mint 22 x86_64 z GCC 13.3.
- Aktualny backend: natywny SDL2 (`linux/gl_sdl2.c`) i OpenGL compatibility. Stary `linux/gl_sdl.c` pozostaje wyłącznie jako punkt odniesienia i nie jest kompilowany.
- Software renderer jest wyłączony. Jedyny aktywny renderer to `ref_gl.so` ładowany przez `vid_ref gl`.
- Minimalna i domyślna rozdzielczość to 800x600. Menu obsługuje także presety do 3840x2160, tryb pulpitu i custom.
- `vid_fullscreen`: 0=okno, 1=exclusive, 2=fullscreen desktop. `vid_display` wybiera monitor SDL, `gl_mode -1` używa `vid_width`/`vid_height`, a `gl_mode -2` natywnej rozdzielczości wybranego monitora. Okno jest ograniczane do usable bounds, a drawable do High DPI.
- Audio SDL2 jest zaimplementowane i zaakceptowane przez użytkownika (`linux/snd_sdl2.c`, 16-bit stereo, ring buffer i callback SDL). Domyślne jest `s_initsound 1`; można je wyłączyć przez `s_initsound 0`. Stary OSS `linux/snd_linux.c` nie jest kompilowany.
- Mysz jest przechwytywana w trakcie rozgrywki fullscreen; w oknie wymaga `_windowed_mouse 1`. Menu i konsola zwalniają grab i pokazują kursor. `IN_Frame` w `linux/vid_so.c` steruje tym na podstawie `cl.refresh_prepped` i `cls.key_dest` — nie usuwaj tej logiki.
- Oryginalne zasoby znajdują się w `/home/user/.quake2`, ale nie wolno ich modyfikować ani instalować tam plików bez wyraźnej prośby użytkownika.
- Jeżeli istnieje `reference/quake2idsoftware/`, używaj go wyłącznie jako źródła porównawczego. Nie dołączaj go do buildu, nie nadpisuj nim plików R1Q2 i nie przenoś zmian mechanicznie bez analizy różnic.

## Budowanie

```sh
make -C binaries -j"$(nproc)"
```

Pełna rekompilacja:

```sh
make -C binaries distclean
make -C binaries -j"$(nproc)"
```

Artefakty: `binaries/client/quake2`, `binaries/r1q2ded/r1q2ded`, `binaries/game/gamex86_64.so`, `binaries/ref_gl/ref_gl.so`.

## Zasady zmian

- Wprowadzaj małe, ukierunkowane modyfikacje zgodne z istniejącym stylem kodu.
- Po każdej zmianie zbuduj projekt i wykonaj możliwe testy; nie deklaruj pełnego działania gry bez testu z oryginalnymi zasobami.
- Nie mieszaj migracji SDL2, audio, dynamicznych rozdzielczości, cząsteczek i optymalizacji w jednym etapie.
- Nie przechodź na OpenGL core ani shadery — nie jest to potrzebne do obecnych celów.
- Nie usuwaj starego SDL 1.2 przed uzyskaniem pełnej zgodności i przetestowaniem SDL2.
- Nie naprawiaj masowo wszystkich historycznych ostrzeżeń; priorytet mają potwierdzone błędy poprawności.
- Nie zastępuj komend, aliasów ani nietypowych hacków bez sprawdzenia ich roli w konfiguracji i modach. `newgame` jest punktem rozszerzenia używanym przez konfigurację użytkownika i należy go zachować.
- Nie wykonuj commitów, pushów ani innych mutacji Git bez wyraźnej zgody użytkownika.

## Testy i raportowanie

Podstawowy zakres: start do menu, `map q2dm1`, `map base1`, start easy/medium z menu, wejście, blaster, rozdzielczości, `vid_restart`, save/load oraz wyjście/ponowny start.

Testy zastępcze bez oryginalnych danych wykonuj wyłącznie w `/tmp/opencode`. Wyraźnie oddzielaj wyniki testów syntetycznych od testów użytkownika z właściwą grą.

Po etapie uzupełnij w `PORTING_LINUX.md`: wykonane zmiany, przyczynę problemu, wyniki kompilacji/testów, testy oczekujące na użytkownika i kolejny krok.
