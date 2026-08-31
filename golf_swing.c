/*
 * golf_swing.c
 *
 * A GTK4 golf swing simulator modelled on David Duval's swing:
 *   - strong grip (closed clubface at the top of the backswing)
 *   - compact, powerful coil (club short of parallel at the top)
 *   - late wrist release (lag) into impact, slightly closed face, high finish
 *
 * The swing is animated as a two-lever system: the left arm rotates about the
 * shoulder pivot while the club hinges at the wrist, so the wrists visibly set
 * on the backswing and release into impact. The upper body coils (shoulder
 * tilt) and the hips shift toward the target, so the whole body moves.
 *
 * Build:  gcc golf_swing.c -o golf_swing $(pkg-config --cflags --libs gtk4) -lm
 */

#include <gtk/gtk.h>
#include <math.h>

#define FRAME_MS 16      /* ~60 FPS timer */
#define DURATION 1.6     /* full swing duration in seconds at 1.0x speed */
#define ARM_LEN  52.0    /* left arm length, pixels */
#define TRAIL    48      /* clubhead motion-trail length */
#define BALL_TEE 24.0    /* ball sits on a tee this high (px) */
#define PLANE_DEG 50.0   /* characteristic swing plane angle (deg) */
#define TEMPO_RATIO 2.9  /* Duval's deliberate backswing:downswing tempo */

/* ------------------------------------------------------------------------- */
/* Swing kinematics                                                          */
/* ------------------------------------------------------------------------- */

/*
 * Angles are measured from straight-down (0 deg), positive toward the golfer's
 * backswing side (screen-left). A direction at angle a maps to screen offset
 *   (-sin a, cos a)
 * so a < 0 points slightly right (behind the ball at address), a = +158 is the
 * compact top, a = 0 is impact and a = -168 is the high finish.
 */
typedef struct {
    double t;    /* normalised swing time, 0..1 */
    double deg;  /* angle in degrees */
} Keyframe;

/* Left-arm angle: the arm rotates about the shoulder pivot. */
static const Keyframe arm_kf[] = {
    { 0.00,  -6.0 },   /* address: arm and club nearly collinear   */
    { 0.30,  55.0 },   /* arm lifts while the wrists begin to set  */
    { 0.46,  96.0 },   /* top: arm across the chest                */
    { 0.49,  96.0 },   /* transition hold                          */
    { 0.58,   0.0 },   /* impact: arm extended down                */
    { 0.72, -80.0 },   /* follow-through                           */
    { 1.00,-148.0 },   /* finish: arm folded high                  */
};

/* Club angle: the club hinges at the wrist relative to the arm. */
static const Keyframe club_kf[] = {
    { 0.00,  -8.0 },   /* address, clubhead just behind the ball   */
    { 0.30, 138.0 },   /* wrists set, club approaching the top     */
    { 0.46, 158.0 },   /* top: Duval stops short of parallel (180) */
    { 0.49, 158.0 },   /* transition hold                          */
    { 0.58,   0.0 },   /* impact                                    */
    { 0.72, -95.0 },   /* follow-through, club parallel on left    */
    { 1.00,-168.0 },   /* high finish                               */
};

static double smoothstep(double u) {
    u = CLAMP(u, 0.0, 1.0);
    return u * u * (3.0 - 2.0 * u);
}

/* Piecewise-eased interpolation over keyframes. The downswing eases in so the
 * club visibly lags then releases through impact; the follow-through eases out. */
static double keyframe_value(const Keyframe *kf, int n, double t) {
    t = CLAMP(t, 0.0, 1.0);
    for (int i = 0; i < n - 1; i++) {
        const Keyframe *a = &kf[i];
        const Keyframe *b = &kf[i + 1];
        if (t <= b->t || i == n - 2) {
            double u = (t - a->t) / (b->t - a->t);
            if (fabs(b->deg - a->deg) < 1e-6)
                u = 0.0;                             /* hold through transition  */
            else if (b->t <= 0.58)
                u = u * u;                           /* ease-in: release into impact */
            else if (a->t >= 0.58)
                u = 1.0 - (1.0 - u) * (1.0 - u);     /* ease-out through finish */
            else
                u = smoothstep(u);
            return a->deg + (b->deg - a->deg) * u;
        }
    }
    return kf[n - 1].deg;
}

static double arm_angle_deg(double t) {
    return keyframe_value(arm_kf, (int)G_N_ELEMENTS(arm_kf), t);
}

static double club_angle_deg(double t) {
    return keyframe_value(club_kf, (int)G_N_ELEMENTS(club_kf), t);
}

/* Wrist hinge (lag): club angle relative to the left arm, in degrees. */
static double lag_deg(double t) {
    return club_angle_deg(t) - arm_angle_deg(t);
}

/*
 * Clubface angle relative to square, in degrees. Positive = closed.
 * Duval's strong grip keeps the face shut at the top and only just square
 * through impact, then releases open in the follow-through.
 */
static double face_angle_deg(double t) {
    t = CLAMP(t, 0.0, 1.0);
    if (t <= 0.30) {                     /* progressively closes on the way up  */
        return 30.0 * (t / 0.30);
    } else if (t <= 0.58) {              /* returns toward square at impact     */
        double u = (t - 0.30) / 0.28;
        return 30.0 - u * 32.0;
    } else {                             /* releases open in the follow-through */
        double u = (t - 0.58) / 0.42;
        return -2.0 - u * 18.0;
    }
}

/* Instantaneous clubhead speed in mph, peaking through impact. */
static double clubhead_speed_mph(double t) {
    double peak = 116.0;                 /* Duval's driver speed territory      */
    double sigma = 0.06;
    double g = exp(-0.5 * pow((t - 0.58) / sigma, 2.0));
    return 4.0 + peak * g;
}

/* Shoulder tilt in pixels: positive dips the left shoulder (screen-left) in the
 * backswing, negative dips the right shoulder through impact and follow-through. */
static double shoulder_tilt(double t) {
    t = CLAMP(t, 0.0, 1.0);
    if (t <= 0.46) {
        return 16.0 * smoothstep(t / 0.46);
    } else if (t <= 0.58) {
        double u = (t - 0.46) / 0.12;
        return 16.0 - 34.0 * smoothstep(u);
    } else {
        double u = (t - 0.58) / 0.42;
        return -18.0 + 18.0 * smoothstep(u);
    }
}

/* Hip shift in pixels: load onto the trail side, then drive onto the lead side. */
static double hip_shift(double t) {
    t = CLAMP(t, 0.0, 1.0);
    if (t <= 0.46) {
        return 7.0 * smoothstep(t / 0.46);
    }
    double u = (t - 0.46) / 0.54;
    return 7.0 - 18.0 * smoothstep(u);
}

/* Head drift in pixels: stays back through impact, then rises to the finish. */
static double head_drift(double t) {
    t = CLAMP(t, 0.0, 1.0);
    if (t <= 0.46) {
        return -4.0 * smoothstep(t / 0.46);
    }
    double u = (t - 0.46) / 0.54;
    return -4.0 + 4.0 * smoothstep(u);
}

/* ------------------------------------------------------------------------- */
/* Application state                                                         */
/* ------------------------------------------------------------------------- */

typedef struct {
    double x, y;
} Point;

typedef struct {
    GtkWidget  *area;
    GtkLabel   *phase_label;
    GtkLabel   *metrics_label;
    guint       tick_id;   /* timeout source id, removed on window destroy */

    double time;         /* swing clock, seconds          */
    double speed;        /* playback speed multiplier     */
    gboolean playing;

    gboolean ball_launched;
    gboolean ball_landed;
    double ball_t;       /* ball flight time, seconds     */

    /* Geometry cached from the last draw for use by the timer. */
    double px, py;       /* swing pivot (left shoulder)    */
    double R;            /* pivot -> clubhead reach        */
    double gy;           /* ground line y                  */
    double bx;           /* ball x                         */
    double by;           /* ball y (on the tee)            */

    Point trail[TRAIL];
    int trail_n;
} SwingApp;

static const char *phase_name(double t) {
    if (t < 0.02) return "ADDRESS";
    if (t < 0.30) return "BACKSWING";
    if (t < 0.49) return "TOP OF BACKSWING";
    if (t < 0.58) return "DOWNSWING";
    if (t < 0.60) return "IMPACT";
    if (t < 0.95) return "FOLLOW-THROUGH";
    return "FINISH";
}

static const char *phase_color(double t) {
    if (t < 0.02) return "#7f8c8d";
    if (t < 0.30) return "#2e6fbf";
    if (t < 0.49) return "#c78a12";
    if (t < 0.58) return "#c0392b";
    if (t < 0.60) return "#e74c3c";
    if (t < 0.95) return "#1e9e50";
    return "#7f8c8d";
}

/* ------------------------------------------------------------------------- */
/* Drawing helpers                                                           */
/* ------------------------------------------------------------------------- */

static void draw_sky(cairo_t *cr, int width, int height) {
    cairo_pattern_t *sky = cairo_pattern_create_linear(0, 0, 0, height);
    cairo_pattern_add_color_stop_rgb(sky, 0.00, 0.48, 0.74, 0.90);
    cairo_pattern_add_color_stop_rgb(sky, 0.70, 0.79, 0.91, 0.96);
    cairo_pattern_add_color_stop_rgb(sky, 1.00, 0.93, 0.97, 0.99);
    cairo_set_source(cr, sky);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);
    cairo_pattern_destroy(sky);

    /* Soft sun. */
    double sx = width * 0.84, sy = height * 0.16;
    cairo_pattern_t *sun = cairo_pattern_create_radial(sx, sy, 6.0, sx, sy, 46.0);
    cairo_pattern_add_color_stop_rgba(sun, 0.0, 1.0, 0.96, 0.75, 0.9);
    cairo_pattern_add_color_stop_rgba(sun, 1.0, 1.0, 0.96, 0.75, 0.0);
    cairo_set_source(cr, sun);
    cairo_arc(cr, sx, sy, 46.0, 0.0, 2.0 * G_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(sun);
}

static void draw_hills(cairo_t *cr, int width, double ground_y) {
    cairo_set_source_rgb(cr, 0.42, 0.62, 0.38);
    cairo_move_to(cr, 0, ground_y);
    cairo_curve_to(cr, width * 0.18, ground_y - 70.0,
                   width * 0.32, ground_y - 90.0,
                   width * 0.50, ground_y);
    cairo_line_to(cr, 0, ground_y);
    cairo_close_path(cr);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 0.36, 0.56, 0.34);
    cairo_move_to(cr, width * 0.30, ground_y);
    cairo_curve_to(cr, width * 0.52, ground_y - 80.0,
                   width * 0.72, ground_y - 80.0,
                   width * 1.00, ground_y);
    cairo_line_to(cr, width * 0.30, ground_y);
    cairo_close_path(cr);
    cairo_fill(cr);
}

static void draw_trees(cairo_t *cr, double x, double ground_y) {
    cairo_set_source_rgb(cr, 0.42, 0.29, 0.18);
    cairo_set_line_width(cr, 5.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, x, ground_y);
    cairo_line_to(cr, x, ground_y - 34.0);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0.20, 0.45, 0.22);
    cairo_arc(cr, x - 12.0, ground_y - 46.0, 14.0, 0.0, 2.0 * G_PI);
    cairo_arc(cr, x + 12.0, ground_y - 46.0, 14.0, 0.0, 2.0 * G_PI);
    cairo_arc(cr, x, ground_y - 58.0, 16.0, 0.0, 2.0 * G_PI);
    cairo_fill(cr);
}

static void draw_ground(cairo_t *cr, int width, int height, double ground_y) {
    cairo_pattern_t *grass = cairo_pattern_create_linear(0, ground_y, 0, height);
    cairo_pattern_add_color_stop_rgb(grass, 0.00, 0.24, 0.56, 0.26);
    cairo_pattern_add_color_stop_rgb(grass, 1.00, 0.13, 0.42, 0.17);
    cairo_set_source(cr, grass);
    cairo_rectangle(cr, 0, ground_y, width, height - ground_y);
    cairo_fill(cr);
    cairo_pattern_destroy(grass);

    /* Mowing stripes. */
    double strip = (height - ground_y) / 6.0;
    for (int i = 0; i < 6; i += 2) {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.05);
        cairo_rectangle(cr, 0, ground_y + i * strip, width, strip);
        cairo_fill(cr);
    }

    /* Turf edge. */
    cairo_set_source_rgba(cr, 0.05, 0.30, 0.10, 0.9);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr, 0, ground_y);
    cairo_line_to(cr, width, ground_y);
    cairo_stroke(cr);

    /* A few grass blades. */
    cairo_set_source_rgba(cr, 0.10, 0.50, 0.20, 0.7);
    cairo_set_line_width(cr, 1.5);
    unsigned int seed = 987654u;
    for (int i = 0; i < 90; i++) {
        seed = (seed * 1103515245u + 12345u) & 0x7fffffff;
        double x = (double)(seed % (unsigned int)width);
        seed = (seed * 1103515245u + 12345u) & 0x7fffffff;
        double h = 4.0 + (double)(seed % 12);
        cairo_move_to(cr, x, ground_y + 1);
        cairo_line_to(cr, x + 1.5, ground_y - h);
        cairo_stroke(cr);
    }
}

static void draw_swing_plane(cairo_t *cr, double px, double py, double R) {
    cairo_set_source_rgba(cr, 0.05, 0.10, 0.25, 0.16);
    cairo_set_line_width(cr, 1.2);
    cairo_set_dash(cr, (double[]){ 4.0, 6.0 }, 2, 0.0);
    cairo_arc(cr, px, py, R, 0.0, 2.0 * G_PI);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0.0);
}

static void draw_trail(cairo_t *cr, const SwingApp *app) {
    for (int i = app->trail_n - 1; i >= 0; i--) {
        double f = 1.0 - (double)i / (double)TRAIL;
        cairo_set_source_rgba(cr, 0.15, 0.35, 0.80, 0.05 + 0.20 * f);
        cairo_arc(cr, app->trail[i].x, app->trail[i].y, 1.0 + 3.2 * f, 0.0, 2.0 * G_PI);
        cairo_fill(cr);
    }
}

static void draw_golfer(cairo_t *cr, double px, double py, double gy,
                        double hx, double hy, double tilt, double hip_dx,
                        double head_dx) {
    double hip_x = px + hip_dx;
    double hip_y = py + 55.0;
    double shx = px + head_dx;
    double shy = py - 4.0;
    double lsx = shx - 20.0, lsy = shy + tilt;
    double rsx = shx + 20.0, rsy = shy - tilt;

    /* Legs with a bent-knee athletic posture. */
    cairo_set_source_rgb(cr, 0.19, 0.19, 0.24);
    cairo_set_line_width(cr, 9.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, hip_x, hip_y);
    cairo_line_to(cr, hip_x - 20.0, gy - 44.0);
    cairo_line_to(cr, hip_x - 30.0, gy - 2.0);
    cairo_move_to(cr, hip_x, hip_y);
    cairo_line_to(cr, hip_x + 20.0, gy - 44.0);
    cairo_line_to(cr, hip_x + 30.0, gy - 2.0);
    cairo_stroke(cr);

    /* Shoes. */
    cairo_set_source_rgb(cr, 0.95, 0.95, 0.97);
    cairo_set_line_width(cr, 7.0);
    cairo_move_to(cr, hip_x - 40.0, gy - 2.0);
    cairo_line_to(cr, hip_x - 22.0, gy - 2.0);
    cairo_move_to(cr, hip_x + 22.0, gy - 2.0);
    cairo_line_to(cr, hip_x + 40.0, gy - 2.0);
    cairo_stroke(cr);

    /* Shadow under the stance. */
    cairo_set_source_rgba(cr, 0.0, 0.15, 0.0, 0.25);
    cairo_save(cr);
    cairo_translate(cr, hip_x, gy - 1.0);
    cairo_scale(cr, 1.0, 0.28);
    cairo_arc(cr, 0.0, 0.0, 50.0, 0.0, 2.0 * G_PI);
    cairo_fill(cr);
    cairo_restore(cr);

    /* Torso (shirt): a trapezoid from the hips up to the tilted shoulders. */
    cairo_set_source_rgb(cr, 0.16, 0.36, 0.72);
    cairo_move_to(cr, hip_x - 11.0, hip_y);
    cairo_line_to(cr, hip_x + 11.0, hip_y);
    cairo_line_to(cr, rsx, rsy);
    cairo_line_to(cr, lsx, lsy);
    cairo_close_path(cr);
    cairo_fill(cr);

    /* Belt line. */
    cairo_set_source_rgb(cr, 0.10, 0.10, 0.14);
    cairo_set_line_width(cr, 5.0);
    cairo_move_to(cr, hip_x - 11.0, hip_y);
    cairo_line_to(cr, hip_x + 11.0, hip_y);
    cairo_stroke(cr);

    /* Head. */
    cairo_set_source_rgb(cr, 0.91, 0.72, 0.55);
    cairo_arc(cr, shx, py - 44.0, 15.0, 0.0, 2.0 * G_PI);
    cairo_fill(cr);

    /* Cap. */
    cairo_set_source_rgb(cr, 0.82, 0.09, 0.09);
    cairo_arc(cr, shx, py - 50.0, 16.5, G_PI, 2.0 * G_PI);
    cairo_fill(cr);
    cairo_rectangle(cr, shx + 3.0, py - 52.0, 16.0, 5.5);
    cairo_fill(cr);

    /* Arms: both shoulders grip the club at the hands. */
    cairo_set_source_rgb(cr, 0.91, 0.72, 0.55);
    cairo_set_line_width(cr, 8.0);
    cairo_move_to(cr, lsx, lsy);
    cairo_line_to(cr, hx, hy);
    cairo_move_to(cr, rsx, rsy);
    cairo_line_to(cr, hx, hy);
    cairo_stroke(cr);
}

static void draw_club(cairo_t *cr, double hx, double hy, double cx, double cy,
                      double face_deg) {
    /* Shaft. */
    cairo_set_source_rgb(cr, 0.20, 0.20, 0.24);
    cairo_set_line_width(cr, 3.5);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, hx, hy);
    cairo_line_to(cr, cx, cy);
    cairo_stroke(cr);

    /* Grip. */
    double gx = hx + (cx - hx) * 0.12;
    double gy2 = hy + (cy - hy) * 0.12;
    cairo_set_source_rgb(cr, 0.09, 0.09, 0.11);
    cairo_set_line_width(cr, 6.5);
    cairo_move_to(cr, hx, hy);
    cairo_line_to(cr, gx, gy2);
    cairo_stroke(cr);

    /* Driver head oriented by the face angle. */
    double dx = cx - hx, dy = cy - hy;
    double len = hypot(dx, dy);
    if (len < 1.0) len = 1.0;
    double ux = dx / len, uy = dy / len;      /* along shaft            */
    double nx = -uy, ny = ux;                 /* perpendicular          */
    double a = face_deg * G_PI / 180.0;
    double rx = nx * cos(a) - ny * sin(a);
    double ry = nx * sin(a) + ny * cos(a);

    cairo_save(cr);
    cairo_translate(cr, cx, cy);
    cairo_rotate(cr, atan2(ry, rx));
    cairo_scale(cr, 1.8, 1.0);

    cairo_pattern_t *hp = cairo_pattern_create_radial(-3.0, -2.0, 1.0,
                                                      0.0, 0.0, 13.0);
    cairo_pattern_add_color_stop_rgb(hp, 0.0, 0.45, 0.45, 0.52);
    cairo_pattern_add_color_stop_rgb(hp, 1.0, 0.16, 0.16, 0.20);
    cairo_set_source(cr, hp);
    cairo_arc(cr, 0.0, 0.0, 11.5, 0.0, 2.0 * G_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(hp);

    /* Face (leading edge). */
    cairo_set_source_rgba(cr, 0.95, 0.95, 1.0, 0.65);
    cairo_rectangle(cr, 6.5, -10.0, 3.0, 20.0);
    cairo_fill(cr);

    cairo_restore(cr);

    /* Hands together on the grip. */
    cairo_set_source_rgb(cr, 0.95, 0.89, 0.80);
    cairo_arc(cr, hx, hy, 6.5, 0.0, 2.0 * G_PI);
    cairo_fill(cr);
}

static void draw_ball_at(cairo_t *cr, double x, double y, double r) {
    cairo_pattern_t *p = cairo_pattern_create_radial(x - r * 0.3, y - r * 0.3,
                                                     r * 0.2, x, y, r);
    cairo_pattern_add_color_stop_rgb(p, 0.0, 1.0, 1.0, 1.0);
    cairo_pattern_add_color_stop_rgb(p, 1.0, 0.82, 0.84, 0.86);
    cairo_set_source(cr, p);
    cairo_arc(cr, x, y, r, 0.0, 2.0 * G_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(p);

    cairo_set_source_rgba(cr, 0.25, 0.28, 0.30, 0.8);
    cairo_set_line_width(cr, 1.0);
    cairo_arc(cr, x, y, r, 0.0, 2.0 * G_PI);
    cairo_stroke(cr);
}

static void draw_tee(cairo_t *cr, double bx, double gy, double top_y) {
    cairo_set_source_rgb(cr, 0.93, 0.93, 0.96);
    cairo_move_to(cr, bx - 3.0, gy);
    cairo_line_to(cr, bx, top_y);
    cairo_line_to(cr, bx + 3.0, gy);
    cairo_close_path(cr);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 0.80, 0.80, 0.85);
    cairo_rectangle(cr, bx - 4.0, top_y - 2.5, 8.0, 3.0);
    cairo_fill(cr);
}

static void draw_impact_flash(cairo_t *cr, double x, double y) {
    cairo_set_source_rgba(cr, 1.0, 0.85, 0.25, 0.9);
    cairo_set_line_width(cr, 2.0);
    for (int i = 0; i < 12; i++) {
        double a = i * G_PI / 6.0;
        cairo_move_to(cr, x + cos(a) * 14.0, y + sin(a) * 14.0);
        cairo_line_to(cr, x + cos(a) * 26.0, y + sin(a) * 26.0);
    }
    cairo_stroke(cr);
}

/* ------------------------------------------------------------------------- */
/* Main draw callback                                                        */
/* ------------------------------------------------------------------------- */

static void draw_cb(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                    gpointer user_data) {
    (void)area;
    SwingApp *app = user_data;

    double ground_y = height * 0.80;
    double pivot_x  = width * 0.52;
    double reach    = MIN(height * 0.55, width * 0.24);
    if (reach < 90.0) reach = 90.0;
    double pivot_y  = ground_y - BALL_TEE - reach;
    double ball_x   = pivot_x;
    double ball_r   = 9.0;
    double ball_y   = ground_y - BALL_TEE;

    /* Cache geometry for the timer-driven trail / ball physics. */
    app->px = pivot_x; app->py = pivot_y; app->R = reach;
    app->gy = ground_y; app->bx = ball_x; app->by = ball_y;

    double t = CLAMP(app->time / DURATION, 0.0, 1.0);
    double arm_a  = arm_angle_deg(t) * G_PI / 180.0;
    double club_a = club_angle_deg(t) * G_PI / 180.0;
    double face   = face_angle_deg(t);
    double tilt   = shoulder_tilt(t);
    double hip_dx = hip_shift(t);
    double head_dx = head_drift(t);

    /* Hands (end of the left arm) and clubhead (end of the club lever). */
    double hx = pivot_x - ARM_LEN * sin(arm_a);
    double hy = pivot_y + ARM_LEN * cos(arm_a);
    double cx = hx - (reach - ARM_LEN) * sin(club_a);
    double cy = hy + (reach - ARM_LEN) * cos(club_a);

    draw_sky(cr, width, height);
    draw_hills(cr, width, ground_y);
    draw_ground(cr, width, height, ground_y);
    draw_trees(cr, width * 0.06, ground_y);
    draw_trees(cr, width * 0.94, ground_y);
    draw_swing_plane(cr, pivot_x, pivot_y, reach);

    draw_golfer(cr, pivot_x, pivot_y, ground_y, hx, hy, tilt, hip_dx, head_dx);
    draw_trail(cr, app);
    draw_club(cr, hx, hy, cx, cy, face);

    /* Ball: on its tee, in flight, or landed. */
    double bdx = ball_x, bdy = ball_y;
    if (app->ball_launched) {
        double v0 = 1500.0, ang = 13.0 * G_PI / 180.0, g = 1500.0;
        double vx = v0 * cos(ang), vy = v0 * sin(ang);
        double tau = app->ball_t;
        bdx = ball_x - vx * tau;
        bdy = ball_y - (vy * tau - 0.5 * g * tau * tau);
        if (bdy > ground_y - ball_r)
            bdy = ground_y - ball_r;
    }
    if (!app->ball_launched)
        draw_tee(cr, ball_x, ground_y, ball_y - ball_r);
    draw_ball_at(cr, bdx, bdy, ball_r);

    /* Impact flash just after contact. */
    if (app->ball_launched && app->ball_t < 0.09)
        draw_impact_flash(cr, ball_x, ball_y);
}

/* ------------------------------------------------------------------------- */
/* Timer, labels and controls                                                */
/* ------------------------------------------------------------------------- */

static void update_labels(SwingApp *app) {
    double t = CLAMP(app->time / DURATION, 0.0, 1.0);

    char *p = g_strdup_printf(
        "<span size='x-large' weight='bold' color='%s'>%s</span>",
        phase_color(t), phase_name(t));
    gtk_label_set_markup(app->phase_label, p);
    g_free(p);

    double spd = clubhead_speed_mph(t);
    double face = face_angle_deg(t);
    double lag = lag_deg(t);
    const char *desc = face >= 1.0 ? "closed" : (face <= -1.0 ? "open" : "square");

    char *m = g_strdup_printf(
        "Clubhead speed: <b>%.0f mph</b>   •   Wrist lag: <b>%.0f°</b>   •   "
        "Face: <b>%.1f° %s</b>   •   Tempo: <b>%.1f : 1</b>",
        spd, lag, fabs(face), desc, TEMPO_RATIO);
    gtk_label_set_markup(app->metrics_label, m);
    g_free(m);
}

static void push_trail(SwingApp *app) {
    double t = CLAMP(app->time / DURATION, 0.0, 1.0);
    double arm_a = arm_angle_deg(t) * G_PI / 180.0;
    double club_a = club_angle_deg(t) * G_PI / 180.0;
    double hx = app->px - ARM_LEN * sin(arm_a);
    double hy = app->py + ARM_LEN * cos(arm_a);
    double cx = hx - (app->R - ARM_LEN) * sin(club_a);
    double cy = hy + (app->R - ARM_LEN) * cos(club_a);

    for (int i = TRAIL - 1; i > 0; i--)
        app->trail[i] = app->trail[i - 1];
    app->trail[0].x = cx;
    app->trail[0].y = cy;
    if (app->trail_n < TRAIL)
        app->trail_n++;
}

static gboolean on_tick(gpointer user_data) {
    SwingApp *app = user_data;
    double dt = 0.016;

    if (app->playing) {
        app->time += dt * app->speed;
        if (app->time >= 0.58 * DURATION && !app->ball_launched)
            app->ball_launched = TRUE;
        if (app->time >= DURATION) {
            app->time = DURATION;
            app->playing = FALSE;
        }
    }

    if (app->ball_launched && !app->ball_landed) {
        app->ball_t += dt;
        /* Landed when the ball returns to the turf. */
        if (app->ball_t > 1.6)
            app->ball_landed = TRUE;
    }

    if (app->playing || (app->ball_launched && !app->ball_landed))
        push_trail(app);

    update_labels(app);
    gtk_widget_queue_draw(app->area);
    return G_SOURCE_CONTINUE;
}

static void on_swing(GtkButton *button, gpointer user_data) {
    (void)button;
    SwingApp *app = user_data;
    if (app->time >= DURATION) {   /* restart from address after a finish */
        app->time = 0.0;
        app->ball_launched = FALSE;
        app->ball_landed = FALSE;
        app->ball_t = 0.0;
        app->trail_n = 0;
    }
    app->playing = TRUE;
    gtk_widget_queue_draw(app->area);
}

static void on_reset(GtkButton *button, gpointer user_data) {
    (void)button;
    SwingApp *app = user_data;
    app->time = 0.0;
    app->playing = FALSE;
    app->ball_launched = FALSE;
    app->ball_landed = FALSE;
    app->ball_t = 0.0;
    app->trail_n = 0;
    update_labels(app);
    gtk_widget_queue_draw(app->area);
}

static void on_speed_changed(GtkRange *range, gpointer user_data) {
    SwingApp *app = user_data;
    app->speed = gtk_range_get_value(range);
}

/* Stop the animation timer before the window's widgets are finalized, so the
 * timer never touches destroyed labels or the drawing area during shutdown. */
static void on_window_destroy(GtkWidget *window, gpointer user_data) {
    (void)window;
    SwingApp *app = user_data;
    if (app->tick_id != 0) {
        g_source_remove(app->tick_id);
        app->tick_id = 0;
    }
}

/* ------------------------------------------------------------------------- */
/* UI construction                                                           */
/* ------------------------------------------------------------------------- */

static void activate(GtkApplication *gtk_app, gpointer user_data) {
    (void)user_data;
    SwingApp *app = g_new0(SwingApp, 1);
    app->speed = 1.0;
    app->gy = 560.0;   /* sane default until the first draw */

    GtkWidget *window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(window), "David Duval Golf Swing Simulator");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 720);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), vbox);

    /* Canvas. */
    GtkWidget *area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(area, TRUE);
    gtk_widget_set_vexpand(area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_cb, app, NULL);
    gtk_box_append(GTK_BOX(vbox), area);
    app->area = area;

    /* Data panel. */
    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(panel, 16);
    gtk_widget_set_margin_end(panel, 16);
    gtk_widget_set_margin_top(panel, 10);
    gtk_widget_set_margin_bottom(panel, 12);
    gtk_box_append(GTK_BOX(vbox), panel);

    app->phase_label = GTK_LABEL(gtk_label_new(NULL));
    gtk_widget_set_halign(GTK_WIDGET(app->phase_label), GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(panel), GTK_WIDGET(app->phase_label));

    app->metrics_label = GTK_LABEL(gtk_label_new(NULL));
    gtk_widget_set_halign(GTK_WIDGET(app->metrics_label), GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(panel), GTK_WIDGET(app->metrics_label));

    GtkWidget *info = gtk_label_new(
        "<span color='#666666'>David Duval signature: strong grip · closed "
        "clubface at the top · compact, powerful coil</span>");
    gtk_label_set_use_markup(GTK_LABEL(info), TRUE);
    gtk_widget_set_halign(info, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(panel), info);

    /* Controls. */
    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(controls, 16);
    gtk_widget_set_margin_end(controls, 16);
    gtk_widget_set_margin_bottom(controls, 14);
    gtk_box_append(GTK_BOX(vbox), controls);

    GtkWidget *swing_btn = gtk_button_new_with_label("▶  Swing");
    g_signal_connect(swing_btn, "clicked", G_CALLBACK(on_swing), app);
    gtk_box_append(GTK_BOX(controls), swing_btn);

    GtkWidget *reset_btn = gtk_button_new_with_label("Reset");
    g_signal_connect(reset_btn, "clicked", G_CALLBACK(on_reset), app);
    gtk_box_append(GTK_BOX(controls), reset_btn);

    gtk_box_append(GTK_BOX(controls), gtk_separator_new(GTK_ORIENTATION_VERTICAL));

    GtkWidget *speed_lbl = gtk_label_new("Speed:");
    gtk_box_append(GTK_BOX(controls), speed_lbl);

    GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                0.25, 2.0, 0.05);
    gtk_range_set_value(GTK_RANGE(scale), 1.0);
    gtk_scale_set_digits(GTK_SCALE(scale), 2);
    gtk_widget_set_size_request(scale, 180, -1);
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_speed_changed), app);
    gtk_box_append(GTK_BOX(controls), scale);

    update_labels(app);

    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), app);
    app->tick_id = g_timeout_add(FRAME_MS, on_tick, app);
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("com.example.GolfSwingDuval",
                                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
