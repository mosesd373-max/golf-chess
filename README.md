# golf-chess

Two small GTK4 demo programs written in C.

## Contents

| File | Description |
|------|-------------|
| `golf_swing.c` | Animated golf swing simulator modelled on David Duval's swing, with a two-lever arm/club model, body coil, and a motion trail. |
| `chess_board.c` | Interactive chess board with Unicode piece rendering, piece selection, and turn handling. |

## Requirements

- A C compiler (GCC)
- GTK 4 development libraries (`libgtk-4-dev` on Debian/Ubuntu)

## Build

```sh
# Golf swing simulator (uses math.h)
gcc golf_swing.c -o golf_swing $(pkg-config --cflags --libs gtk4) -lm

# Chess board
gcc chess_board.c -o chess_board $(pkg-config --cflags --libs gtk4)
```

## Run

```sh
./golf_swing
./chess_board
```
