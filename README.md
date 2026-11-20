# ZeroPower

Ahorro de batería y ajustes de energía para el [M5Stack Cardputer Zero](https://docs.m5stack.com/)
(Raspberry Pi CM0, 512 MB, pantalla de 320×170, Debian 13). En el lanzador se llama **Power**.

El Cardputer Zero gasta unos **380-420 mA** sin hacer nada: con su batería de 1.750 mAh son
poco más de 4 horas. Buena parte se va en cosas que en un aparato de bolsillo no se usan (el
chip Ethernet USB interno, el escritorio para una pantalla HDMI, el Bluetooth, servicios de
red) y en el lanzador, que sigue funcionando y con el audio abierto aunque la pantalla esté
apagada. ZeroPower deja apagar cada cosa **para siempre** desde una pantalla, ver al momento
cuánto consume el aparato y **deshacerlo** todo igual de fácil.

## Qué hace

- **Battery**: el consumo de ahora (media de los últimos 30 s, del medidor de la batería
  bq27220), la carga, el voltaje y el tiempo que queda a ese ritmo, con el último minuto en
  barras. `R` empieza la media de cero: así se ve lo que ahorra cada ajuste.
- **Hardware**:
  - **USB Ethernet**: *On*, *Auto-suspend* (el chip duerme si no hay cable: ~100 mA menos) u
    *Off* (desactivado).
  - **Wi-Fi**: encendido o apagado (sin red, ni tienda, ni acceso remoto: pide confirmar).
  - **Bluetooth**: radio y servicio.
  - **HDMI desktop**: el escritorio para una pantalla HDMI (`lightdm`); apagado libera RAM y
    CPU. El lanzador no depende de él.
  - **GPU memory**: la RAM que se reserva para el firmware de la GPU. La pantalla interna es
    un panel SPI que no la usa: con **16 MB** quedan 48 MB más para Linux. Vale tras reiniciar.
- **Services**: **Tailscale** (pide confirmar), **Background extras** (NFS, rpcbind, avahi y
  los temporizadores de apt y man-db) y **Logs in RAM** (el journal en memoria: menos
  escrituras en la tarjeta).
- **Power saving** (lo hace el servicio `zeropowerd`):
  - **Screen-off saver**: con la pantalla apagada, pausa el lanzador y aparca su audio para
    que duerman la tarjeta de sonido y el amplificador. Una tecla lo despierta. Si hay una app
    abierta no pausa nada.
  - **Quiet launcher**: el lanzador sin sonido de teclas, siempre.
  - **Low battery off**: apaga ordenadamente al 5, 8, 10 o 15 % (tres lecturas seguidas).

Cada ajuste se queda puesto en el propio sistema (servicios desactivados, una regla de `udev`,
`config.txt`, la radio del wifi), así que sobrevive a los reinicios sin que nada tenga que
volver a aplicarlo. **Al desinstalar ZeroPower, todo lo que cambió vuelve a como estaba.**

## Teclas

| Dónde | Tecla | Qué hace |
|---|---|---|
| Todas | `Fn`+`H` · `Fn`+`J` · `T` | Ayuda · captura · claro/oscuro |
| | `Esc` / mantener `Esc` | Atrás / salir |
| Menú | `F` / `X` · `ENTER` | Elegir · abrir |
| Ajustes | `F` / `X` | Elegir ajuste |
| | `Z` / `C` | Elegir valor (queda marcado con `•`) |
| | `ENTER` | Aplicar (sin valor elegido, pasa al siguiente) |
| | `Esc` | Descartar lo elegido / volver |
| Battery | `R` | Empezar la media de cero |

## Cómo funciona

- **La app** corre como tu usuario. Lee el estado real con
  `/usr/lib/zeropower/zeropower-helper status` (sin root).
- **Para cambiar algo** llama a `sudo -n /usr/lib/zeropower/zeropower-helper set <ajuste> <valor>`.
  `/etc/sudoers.d/zeropower` permite a los del grupo `sudo` ejecutar **sólo ese ayudante** sin
  contraseña. Es un script de bash corto que acepta una lista cerrada de ajustes y valores;
  la primera vez que cambia algo guarda su valor original en `/var/lib/zeropower/original/`
  para poder devolverlo (`zeropower-helper restore`, que se ejecuta al desinstalar).
- **`zeropowerd`** es un servicio de usuario (como el propio lanzador, `APPLaunch.service`),
  sin root. Lee sus opciones de `~/.config/zeropower/power.conf`, que la app reescribe. Duerme
  esperando al teclado y mira el brillo cada segundo y la batería cada 20 s.

Ver lo que hace: `journalctl --user -u zeropowerd -f`.

## Medidas

Las del autor de cz-battery-saver en un Cardputer Zero (pantalla al 30 %, wifi conectado),
que son las que motivaron cada ajuste:

| Cambio | Consumo |
|---|---|
| Tal cual | ~416 mA |
| Ethernet USB en auto-suspend | ~311 mA |
| + audio del lanzador aparcado | ~277 mA |
| Pantalla apagada + lanzador en pausa (con lo anterior) | ~233 mA |

En el Cardputer de este repositorio, tal cual y en reposo: **378 mA** (wifi, brillo al 100 %).
Las aplicaciones hechas con la plantilla oficial además tenían un núcleo de la CPU al 100 %
mientras estaban abiertas (arreglado en esta y en las demás apps de este autor).

## Instalar

En el aparato, desde la tienda del Cardputer Zero, o con el `.deb`:

```sh
sudo apt install ./ZeroPower_<versión>_rev1_arm64.deb
```

Desinstalar (devuelve todos los ajustes): `sudo apt remove zeropower`.

## Compilar

```sh
Tools/compilar.sh pruebas     # simulador de escritorio (SDL2) y pruebas
Tools/compilar.sh deb         # dist/ZeroPower_<versión>_rev1_arm64.deb para el aparato
Tools/compilar.sh simulador   # abre el simulador en este escritorio (X11)
```

Usa Docker si lo hay y, si no, las herramientas de la máquina (las de `Tools/Dockerfile`). En
el simulador no está el ayudante: la app simula el sistema en memoria.

## Créditos y licencia

- Hecha sobre la [plantilla oficial](https://github.com/CardputerZero/Template) del Cardputer
  Zero (MIT, © M5Stack CardputerZero Community), con LVGL (MIT) e iconos Phosphor (MIT; ver
  `assets/fonts/LICENSE.txt`).
- Las ideas del ahorro con la pantalla apagada, el audio del lanzador y el Ethernet en
  auto-suspend, y sus medidas, son de
  [cz-battery-saver](https://github.com/InstantComet/cz-battery-saver) de InstantComet (MIT).
  El código de ZeroPower es propio.
- El código es MIT (ver `LICENSE`).

## Estado

**0.1.0** (20-nov-2026): primera versión. Probada en el simulador; en el aparato, el ayudante
lee bien el estado del sistema. Falta probar en el aparato cada ajuste y el servicio.
