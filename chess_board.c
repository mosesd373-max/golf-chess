#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>

#define BOARD_SIZE 8
#define CELL_SIZE 80

/* Piece types */
typedef enum {
    EMPTY = 0,
    PAWN,
    ROOK,
    KNIGHT,
    BISHOP,
    QUEEN,
    KING
} PieceType;

/* Piece colors */
typedef enum {
    NO_COLOR = 0,
    WHITE,
    BLACK
} PieceColor;

typedef struct {
    PieceType type;
    PieceColor color;
} Piece;

typedef struct {
    Piece board[BOARD_SIZE][BOARD_SIZE];
    int selected_row;
    int selected_col;
    int has_selection;
    PieceColor turn;
} ChessGame;

/* Unicode chess piece symbols */
static const char *piece_symbols[2][7] = {
    /* White pieces */
    {"", "\u2659", "\u2656", "\u2658", "\u2657", "\u2655", "\u2654"},
    /* Black pieces */
    {"", "\u265F", "\u265C", "\u265E", "\u265D", "\u265B", "\u265A"}
};

static void init_board(ChessGame *game) {
    memset(game->board, 0, sizeof(game->board));
    game->selected_row = -1;
    game->selected_col = -1;
    game->has_selection = FALSE;
    game->turn = WHITE;

    /* Place pawns */
    for (int col = 0; col < BOARD_SIZE; col++) {
        game->board[1][col].type = PAWN;
        game->board[1][col].color = BLACK;
        game->board[6][col].type = PAWN;
        game->board[6][col].color = WHITE;
    }

    /* Place back row pieces */
    PieceType back_row[8] = {ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK};

    for (int col = 0; col < BOARD_SIZE; col++) {
        game->board[0][col].type = back_row[col];
        game->board[0][col].color = BLACK;
        game->board[7][col].type = back_row[col];
        game->board[7][col].color = WHITE;
    }
}

/* Check if a position is within the board */
static int in_bounds(int row, int col) {
    return row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE;
}

/* Check if a piece can move to a square (empty or enemy piece) */
static int can_capture(ChessGame *game, int row, int col, PieceColor color) {
    if (!in_bounds(row, col))
        return FALSE;
    if (game->board[row][col].type == EMPTY)
        return TRUE;
    return game->board[row][col].color != color;
}

/* Check if a path is clear between two squares (for sliding pieces) */
static int is_path_clear(ChessGame *game, int from_row, int from_col, int to_row, int to_col) {
    int dr = (to_row > from_row) ? 1 : (to_row < from_row) ? -1 : 0;
    int dc = (to_col > from_col) ? 1 : (to_col < from_col) ? -1 : 0;

    int row = from_row + dr;
    int col = from_col + dc;

    while (row != to_row || col != to_col) {
        if (game->board[row][col].type != EMPTY)
            return FALSE;
        row += dr;
        col += dc;
    }
    return TRUE;
}

/* Check if a move is valid for a given piece */
static int is_valid_move(ChessGame *game, int from_row, int from_col, int to_row, int to_col) {
    Piece piece = game->board[from_row][from_col];
    int dr = to_row - from_row;
    int dc = to_col - from_col;
    int abs_dr = abs(dr);
    int abs_dc = abs(dc);

    if (piece.type == EMPTY)
        return FALSE;
    if (from_row == to_row && from_col == to_col)
        return FALSE;
    if (!can_capture(game, to_row, to_col, piece.color))
        return FALSE;

    switch (piece.type) {
        case PAWN:
            /* Pawns move forward, direction depends on color */
            {
                int dir = (piece.color == WHITE) ? -1 : 1;
                int start_row = (piece.color == WHITE) ? 6 : 1;

                /* Move one square forward */
                if (dc == 0 && dr == dir && game->board[to_row][to_col].type == EMPTY)
                    return TRUE;

                /* Move two squares forward from starting position */
                if (dc == 0 && dr == 2 * dir && from_row == start_row &&
                    game->board[to_row][to_col].type == EMPTY &&
                    game->board[from_row + dir][from_col].type == EMPTY)
                    return TRUE;

                /* Capture diagonally */
                if (abs_dc == 1 && dr == dir && game->board[to_row][to_col].type != EMPTY)
                    return TRUE;
            }
            return FALSE;

        case ROOK:
            /* Rook moves horizontally or vertically */
            if (from_row == to_row || from_col == to_col)
                return is_path_clear(game, from_row, from_col, to_row, to_col);
            return FALSE;

        case KNIGHT:
            /* Knight moves in an L-shape */
            if ((abs_dr == 2 && abs_dc == 1) || (abs_dr == 1 && abs_dc == 2))
                return TRUE;
            return FALSE;

        case BISHOP:
            /* Bishop moves diagonally */
            if (abs_dr == abs_dc && abs_dr > 0)
                return is_path_clear(game, from_row, from_col, to_row, to_col);
            return FALSE;

        case QUEEN:
            /* Queen moves like rook + bishop */
            if ((from_row == to_row || from_col == to_col) ||
                (abs_dr == abs_dc && abs_dr > 0))
                return is_path_clear(game, from_row, from_col, to_row, to_col);
            return FALSE;

        case KING:
            /* King moves one square in any direction */
            if (abs_dr <= 1 && abs_dc <= 1)
                return TRUE;
            return FALSE;

        default:
            return FALSE;
    }
}

/* Drawing callback for the chess board */
static void draw_board(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
    ChessGame *game = (ChessGame *)data;
    (void)area;
    (void)width;
    (void)height;

    double cell_w = (double)width / BOARD_SIZE;
    double cell_h = (double)height / BOARD_SIZE;

    /* Draw the board squares */
    for (int row = 0; row < BOARD_SIZE; row++) {
        for (int col = 0; col < BOARD_SIZE; col++) {
            double x = col * cell_w;
            double y = row * cell_h;

            /* Alternate colors */
            if ((row + col) % 2 == 0) {
                cairo_set_source_rgb(cr, 0.93, 0.87, 0.75); /* Light square */
            } else {
                cairo_set_source_rgb(cr, 0.55, 0.35, 0.20); /* Dark square */
            }

            cairo_rectangle(cr, x, y, cell_w, cell_h);
            cairo_fill(cr);

            /* Highlight selected square */
            if (game->has_selection && game->selected_row == row && game->selected_col == col) {
                cairo_set_source_rgba(cr, 0.2, 0.8, 0.2, 0.5);
                cairo_rectangle(cr, x, y, cell_w, cell_h);
                cairo_fill(cr);
            }

            /* Highlight valid moves for selected piece */
            if (game->has_selection) {
                Piece sel = game->board[game->selected_row][game->selected_col];
                if (sel.type != EMPTY && is_valid_move(game, game->selected_row, game->selected_col, row, col)) {
                    cairo_set_source_rgba(cr, 0.2, 0.8, 0.2, 0.3);
                    cairo_rectangle(cr, x, y, cell_w, cell_h);
                    cairo_fill(cr);
                }
            }

            /* Draw the piece */
            Piece piece = game->board[row][col];
            if (piece.type != EMPTY) {
                const char *symbol = piece_symbols[piece.color - 1][piece.type];
                cairo_text_extents_t extents;

                cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
                cairo_set_font_size(cr, cell_h * 0.7);

                cairo_text_extents(cr, symbol, &extents);

                double tx = x + (cell_w - extents.width) / 2 - extents.x_bearing;
                double ty = y + (cell_h - extents.height) / 2 - extents.y_bearing;

                /* Draw piece with color */
                if (piece.color == WHITE) {
                    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
                    /* Draw white outline for visibility on light squares */
                    cairo_save(cr);
                    cairo_set_line_width(cr, 2.0);
                    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
                    cairo_move_to(cr, tx, ty);
                    cairo_text_path(cr, symbol);
                    cairo_stroke(cr);
                    cairo_restore(cr);
                    cairo_move_to(cr, tx, ty);
                    cairo_show_text(cr, symbol);
                } else {
                    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
                    cairo_move_to(cr, tx, ty);
                    cairo_show_text(cr, symbol);
                }
            }
        }
    }
}

/* Handle mouse clicks on the board */
static void on_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
    ChessGame *game = (ChessGame *)data;
    (void)n_press;

    GtkWidget *area = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    int width = gtk_widget_get_width(area);
    int height = gtk_widget_get_height(area);

    int col = (int)(x / (width / (double)BOARD_SIZE));
    int row = (int)(y / (height / (double)BOARD_SIZE));

    if (col < 0 || col >= BOARD_SIZE || row < 0 || row >= BOARD_SIZE)
        return;

    if (game->has_selection) {
        int sel_row = game->selected_row;
        int sel_col = game->selected_col;
        Piece sel_piece = game->board[sel_row][sel_col];

        /* If clicking on own piece, select it instead */
        if (game->board[row][col].type != EMPTY && game->board[row][col].color == game->turn) {
            game->selected_row = row;
            game->selected_col = col;
            gtk_widget_queue_draw(area);
            return;
        }

        /* Try to move the selected piece */
        if (sel_piece.type != EMPTY && is_valid_move(game, sel_row, sel_col, row, col)) {
            /* Move the piece */
            game->board[row][col] = sel_piece;
            game->board[sel_row][sel_col].type = EMPTY;
            game->board[sel_row][sel_col].color = NO_COLOR;

            /* Switch turns */
            game->turn = (game->turn == WHITE) ? BLACK : WHITE;

            game->has_selection = FALSE;
            game->selected_row = -1;
            game->selected_col = -1;
        } else {
            /* Invalid move, deselect */
            game->has_selection = FALSE;
            game->selected_row = -1;
            game->selected_col = -1;
        }
    } else {
        /* Select a piece if it belongs to the current player */
        if (game->board[row][col].type != EMPTY && game->board[row][col].color == game->turn) {
            game->selected_row = row;
            game->selected_col = col;
            game->has_selection = TRUE;
        }
    }

    gtk_widget_queue_draw(area);
}

static void activate(GtkApplication *app, gpointer user_data) {
    ChessGame *game = (ChessGame *)user_data;

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Chess Board");
    gtk_window_set_default_size(GTK_WINDOW(window), BOARD_SIZE * CELL_SIZE, BOARD_SIZE * CELL_SIZE);

    GtkWidget *drawing_area = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area), draw_board, game, NULL);
    gtk_widget_set_hexpand(drawing_area, TRUE);
    gtk_widget_set_vexpand(drawing_area, TRUE);

    /* Add click handling */
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_click), game);
    gtk_widget_add_controller(drawing_area, GTK_EVENT_CONTROLLER(click));

    gtk_window_set_child(GTK_WINDOW(window), drawing_area);
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    ChessGame game;
    init_board(&game);

    GtkApplication *app = gtk_application_new("com.example.ChessBoard", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &game);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}