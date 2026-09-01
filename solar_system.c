/*
 * solar_system.c
 *
 * A GTK4 application that animates the Solar System using a GtkDrawingArea
 * and Cairo rendering.
 *
 * Build:  gcc solar_system.c -o solar_system $(pkg-config --cflags --libs gtk4) -lm
 */

#include <gtk/gtk.h>
#include <math.h>

#define PLANET_COUNT 8
#define FRAME_MS 16 /* ~60 FPS */

/*
 * Compromise orbital speeds.
 *
 * Kepler's third law gives angular speed ~ a^-1.5 (Earth = 1.0), but that makes
 * the outer planets almost motionless on screen. Using a^-1.0 keeps the correct
 * ordering and a wide spread while still letting Jupiter..Neptune visibly move.
 */
#define EARTH_SPEED 0.029 /* radians per frame, Earth's orbital rate */
#define SPEED_EXPONENT -1.0

typedef struct {
    const char *name;
    double orbit_radius;     /* semi-major axis, relative to 0..1 of the radius */
    double radius;           /* planet pixel radius */
    double angle;            /* mean anomaly (radians) */
    double speed;            /* mean anomaly per frame (orbital rate) */
    double eccentricity;     /* display eccentricity (exaggerated) */
    double perihelion_angle; /* orientation of the ellipse (radians) */
    GdkRGBA color;
    gboolean has_ring;
} Planet;

typedef struct {
    Planet planets[PLANET_COUNT];
    GdkRGBA star_color;
} SolarSystem;

static void solar_system_init(SolarSystem *ss) {
    GdkRGBA colors[PLANET_COUNT];

    /* Mercury */
    gdk_rgba_parse(&colors[0], "#b5a7a0");
    /* Venus */
    gdk_rgba_parse(&colors[1], "#e6c07b");
    /* Earth */
    gdk_rgba_parse(&colors[2], "#4f8bd4");
    /* Mars */
    gdk_rgba_parse(&colors[3], "#d46b4f");
    /* Jupiter */
    gdk_rgba_parse(&colors[4], "#d4a06b");
    /* Saturn */
    gdk_rgba_parse(&colors[5], "#e8d3a3");
    /* Uranus */
    gdk_rgba_parse(&colors[6], "#9ad6d6");
    /* Neptune */
    gdk_rgba_parse(&colors[7], "#4f6bd4");

    const double orbit_radii[PLANET_COUNT] = { 0.14, 0.20, 0.27, 0.34, 0.46, 0.60, 0.72, 0.82 };
    const double planet_radii[PLANET_COUNT] = { 3.0, 4.5, 5.0, 4.0, 9.0, 8.0, 6.0, 5.5 };
    /* Real semi-major axes in AU, used only to derive relative speeds. */
    const double semi_major_au[PLANET_COUNT] =
        { 0.387, 0.723, 1.0, 1.524, 5.203, 9.537, 19.19, 30.07 };
    /* Exaggerated eccentricities so the ellipticity is clearly visible.
     * Keeps the real ordering (Mercury most, Venus/Neptune least). */
    const double eccentricities[PLANET_COUNT] =
        { 0.30, 0.08, 0.12, 0.22, 0.10, 0.13, 0.09, 0.07 };
    /* Orientation of each orbit (argument of perihelion, radians). */
    const double perihelion_angles[PLANET_COUNT] =
        { 0.79, 1.40, 1.78, 5.87, 0.26, 1.62, 2.97, 0.79 };
    const char *names[PLANET_COUNT] =
        { "Mercury", "Venus", "Earth", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune" };
    const gboolean rings[PLANET_COUNT] =
        { FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE };

    for (int i = 0; i < PLANET_COUNT; i++) {
        ss->planets[i].name = names[i];
        ss->planets[i].orbit_radius = orbit_radii[i];
        ss->planets[i].radius = planet_radii[i];
        ss->planets[i].angle = (i * 0.9); /* stagger starting positions */
        ss->planets[i].speed = EARTH_SPEED * pow(semi_major_au[i], SPEED_EXPONENT);
        ss->planets[i].eccentricity = eccentricities[i];
        ss->planets[i].perihelion_angle = perihelion_angles[i];
        ss->planets[i].color = colors[i];
        ss->planets[i].has_ring = rings[i];
    }

    gdk_rgba_parse(&ss->star_color, "#ffffff");
}

static void draw_stars(cairo_t *cr, int width, int height) {
    /* Deterministic pseudo-random star field. */
    unsigned int seed = 12345;
    for (int i = 0; i < 120; i++) {
        seed = (seed * 1103515245u + 12345u) & 0x7fffffff;
        double x = (double)(seed % (unsigned int)width);
        seed = (seed * 1103515245u + 12345u) & 0x7fffffff;
        double y = (double)(seed % (unsigned int)height);
        seed = (seed * 1103515245u + 12345u) & 0x7fffffff;
        double b = 0.3 + (double)(seed % 100) / 100.0 * 0.7;
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, b);
        cairo_arc(cr, x, y, 0.6, 0.0, 2.0 * G_PI);
        cairo_fill(cr);
    }
}

static void draw_sun(cairo_t *cr, double cx, double cy, double r) {
    /* Outer glow. */
    cairo_pattern_t *glow = cairo_pattern_create_radial(cx, cy, r * 0.2, cx, cy, r * 3.0);
    cairo_pattern_add_color_stop_rgba(glow, 0.0, 1.0, 0.85, 0.2, 0.35);
    cairo_pattern_add_color_stop_rgba(glow, 0.5, 1.0, 0.6, 0.05, 0.12);
    cairo_pattern_add_color_stop_rgba(glow, 1.0, 1.0, 0.5, 0.0, 0.0);
    cairo_set_source(cr, glow);
    cairo_arc(cr, cx, cy, r * 3.0, 0.0, 2.0 * G_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(glow);

    /* Sun body. */
    cairo_pattern_t *body = cairo_pattern_create_radial(cx - r * 0.3, cy - r * 0.3, r * 0.1,
                                                        cx, cy, r);
    cairo_pattern_add_color_stop_rgba(body, 0.0, 1.0, 0.98, 0.75, 1.0);
    cairo_pattern_add_color_stop_rgba(body, 1.0, 1.0, 0.55, 0.05, 1.0);
    cairo_set_source(cr, body);
    cairo_arc(cr, cx, cy, r, 0.0, 2.0 * G_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(body);
}

static void draw_planet(cairo_t *cr, const Planet *p, double x, double y) {
    /* Slight shading to give the planet a 3D look. */
    cairo_pattern_t *shade = cairo_pattern_create_radial(x - p->radius * 0.35,
                                                         y - p->radius * 0.35, p->radius * 0.15,
                                                         x, y, p->radius);
    cairo_pattern_add_color_stop_rgba(shade, 0.0,
                                      p->color.red + 0.25, p->color.green + 0.25,
                                      p->color.blue + 0.25, 1.0);
    cairo_pattern_add_color_stop_rgba(shade, 1.0,
                                      p->color.red, p->color.green, p->color.blue, 1.0);
    cairo_set_source(cr, shade);
    cairo_arc(cr, x, y, p->radius, 0.0, 2.0 * G_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(shade);

    if (p->has_ring) {
        cairo_set_source_rgba(cr, p->color.red, p->color.green, p->color.blue, 0.55);
        cairo_set_line_width(cr, p->radius * 0.45);
        cairo_save(cr);
        cairo_translate(cr, x, y);
        cairo_scale(cr, 1.0, 0.45); /* flatten ring into an ellipse */
        cairo_arc(cr, 0.0, 0.0, p->radius * 1.8, 0.0, 2.0 * G_PI);
        cairo_stroke(cr);
        cairo_restore(cr);
    }
}

static void draw_planet_label(cairo_t *cr, const Planet *p, double x, double y) {
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.55);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 9.0);
    cairo_move_to(cr, x + p->radius + 3.0, y - p->radius - 3.0);
    cairo_show_text(cr, p->name);
}

/*
 * Compute the planet's screen position on an elliptical orbit with the Sun at
 * one focus. Solves Kepler's equation (M = E - e*sin(E)) via Newton's method,
 * so the planet speeds up near perihelion and slows near aphelion.
 */
static void planet_position(const Planet *p, double cx, double cy, double max_radius,
                            double *out_x, double *out_y) {
    double a = p->orbit_radius * max_radius; /* semi-major axis */
    double e = p->eccentricity;
    double b = a * sqrt(1.0 - e * e);        /* semi-minor axis */
    double phi = p->perihelion_angle;

    /* Solve Kepler's equation for the eccentric anomaly E. */
    double M = p->angle;
    double E = M;
    for (int i = 0; i < 6; i++) {
        E -= (E - e * sin(E) - M) / (1.0 - e * cos(E));
    }

    /* Position relative to the Sun (focus), perihelion along +x. */
    double px = a * (cos(E) - e);
    double py = b * sin(E);

    /* Rotate by the perihelion argument and offset to the Sun's location. */
    *out_x = cx + px * cos(phi) - py * sin(phi);
    *out_y = cy + px * sin(phi) + py * cos(phi);
}

static void draw_cb(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                    gpointer user_data) {
    SolarSystem *ss = user_data;

    /* Background. */
    cairo_set_source_rgb(cr, 0.02, 0.02, 0.05);
    cairo_paint(cr);

    draw_stars(cr, width, height);

    double cx = width / 2.0;
    double cy = height / 2.0;
    double max_radius = MIN(width, height) / 2.0 - 20.0;

    /* Orbit paths (ellipses, Sun at one focus). */
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
    cairo_set_line_width(cr, 1.0);
    for (int i = 0; i < PLANET_COUNT; i++) {
        const Planet *p = &ss->planets[i];
        double a = p->orbit_radius * max_radius;
        double b = a * sqrt(1.0 - p->eccentricity * p->eccentricity);
        double c = a * p->eccentricity; /* focus offset from ellipse center */

        cairo_save(cr);
        cairo_translate(cr, cx - c * cos(p->perihelion_angle),
                        cy - c * sin(p->perihelion_angle));
        cairo_rotate(cr, p->perihelion_angle);
        cairo_scale(cr, 1.0, b / a);
        cairo_arc(cr, 0.0, 0.0, a, 0.0, 2.0 * G_PI);
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    draw_sun(cr, cx, cy, 14.0);

    for (int i = 0; i < PLANET_COUNT; i++) {
        Planet *p = &ss->planets[i];
        double x, y;
        planet_position(p, cx, cy, max_radius, &x, &y);
        draw_planet(cr, p, x, y);
        draw_planet_label(cr, p, x, y);
    }
}

typedef struct {
    SolarSystem *ss;
    GtkWidget *area;
} Animation;

static gboolean on_tick(gpointer user_data) {
    Animation *anim = user_data;
    SolarSystem *ss = anim->ss;

    for (int i = 0; i < PLANET_COUNT; i++) {
        ss->planets[i].angle += ss->planets[i].speed;
        if (ss->planets[i].angle > 2.0 * G_PI) {
            ss->planets[i].angle -= 2.0 * G_PI;
        }
    }

    gtk_widget_queue_draw(anim->area);
    return G_SOURCE_CONTINUE;
}

static void activate(GtkApplication *app, gpointer user_data) {
    SolarSystem *ss = g_new0(SolarSystem, 1);
    solar_system_init(ss);

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Solar System");
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 700);

    GtkWidget *drawing_area = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area), draw_cb, ss, g_free);
    gtk_window_set_child(GTK_WINDOW(window), drawing_area);

    Animation *anim = g_new(Animation, 1);
    anim->ss = ss;
    anim->area = drawing_area;

    /* Single timer drives the animation: update angles, then redraw. */
    g_timeout_add(FRAME_MS, on_tick, anim);

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("com.example.SolarSystem",
                                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
