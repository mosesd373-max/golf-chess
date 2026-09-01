# golf-chess

Small GTK4 demo programs written in C.

## Contents

| File | Description |
|------|-------------|
| `golf_swing.c` | Animated golf swing simulator modelled on David Duval's swing, with a two-lever arm/club model, body coil, and a motion trail. |
| `chess_board.c` | Interactive chess board with Unicode piece rendering, piece selection, and turn handling. |
| `solar_system.c` | Animated Solar System rendered with Cairo, with 8 planets, elliptical orbits, and Saturn's rings. |

## Requirements

- A C compiler (GCC)
- GTK 4 development libraries (`libgtk-4-dev` on Debian/Ubuntu)

## Build

```sh
# Golf swing simulator (uses math.h)
gcc golf_swing.c -o golf_swing $(pkg-config --cflags --libs gtk4) -lm

# Chess board
gcc chess_board.c -o chess_board $(pkg-config --cflags --libs gtk4)

# Solar System animation (uses math.h)
gcc solar_system.c -o solar_system $(pkg-config --cflags --libs gtk4) -lm
```

## Run

```sh
./golf_swing
./chess_board
./solar_system
```
