#include <stdlib.h>
#include <stdarg.h>
#include <clutter/clutter.h>
#include "clutter-box2d.h"

#define ASSETS_DIR "./"

void start_demo (gint demo_no);

typedef struct Demo
{
  gchar *title;
  void (*create)  (struct Demo *demo);
  void (*destroy) (struct Demo *demo);

  /* for use with create and destroy */
  ClutterActor *world;
  ClutterActor *actor;
  gpointer      user_data;
} Demo;

void demo_about   (Demo *demo);
void demo_pyramid (Demo *demo);
void demo_slides  (Demo *demo);
void demo_control (Demo *demo);

Demo demos[] =
{
  {"about",   demo_about},
  {"control", demo_control},
  {"slides",  demo_slides},
  {"pyramid", demo_pyramid},
  {NULL, NULL}
};

gint  n_demos = sizeof (demos) / sizeof (demos[0]) - 1;
gint  current_demo = -1;
gboolean playing = TRUE;

static gboolean label_action_press (ClutterActor *actor,
                                    ClutterEvent *event,
                                    gpointer      data)
{
  void       (*action) (ClutterActor *label,
                        gpointer      userdata) = data;
  gpointer user_data = g_object_get_data (G_OBJECT (actor), "la-ud");

  if (action)
    action (actor, user_data);
  else
    g_print ("no action\n");

  return FALSE;
}

static gboolean label_action_enter (ClutterActor *actor,
                                    ClutterEvent *event,
                                    gpointer      data)
{
  clutter_actor_set_opacity (actor, 0xff);
  return FALSE;
}

static gboolean label_action_leave (ClutterActor *actor,
                                    ClutterEvent *event,
                                    gpointer      data)
{
  clutter_actor_set_opacity (actor, 0x77);
  return FALSE;
}

ClutterActor *
label_action (const gchar *font,
              const gchar *label,
              const gchar *color,
              void       (*action) (ClutterActor *label,
                                    gpointer      userdata),
              gpointer     userdata
              )
{
  ClutterActor *actor;
  ClutterColor  ccol;

  clutter_color_parse (color, &ccol);
  actor = clutter_label_new_full (font, label, &ccol);
  clutter_actor_set_reactive (actor, TRUE);
  clutter_actor_set_opacity (actor, 0x77);

  g_signal_connect (actor, "button-press-event",
                            G_CALLBACK (label_action_press), action);
  g_signal_connect (actor, "enter-event",
                            G_CALLBACK (label_action_enter), action);
  g_signal_connect (actor, "leave-event",
                            G_CALLBACK (label_action_leave), action);
  g_object_set_data (G_OBJECT (actor), "la-ud", userdata);

  return actor;
}

typedef struct IncrementalInfo
{
  gint x0;
  gint y0;
  gint x;
  gint y;
  gint width;
  gint max_height;
  gint padding_bottom;
} IncrementalInfo;

static IncrementalInfo *
incremental_init (ClutterGroup *group,
                  gint          x0,
                  gint          y0,
                  gint          width,
                  gint          padding_bottom)
{
  IncrementalInfo *info;

  info = g_object_get_data(G_OBJECT (group), "incremental-info");
  if (!info)
    {
      info = g_malloc0 (sizeof (IncrementalInfo));
      g_object_set_data (G_OBJECT (group), "incremental-info", info);
    }
  info->x0 = x0;
  info->y0 = y0;
  info->width = width;
  info->x = x0;
  info->y = y0;
  info->padding_bottom = padding_bottom;
  return info;
}

static void
incremental_add (ClutterGroup *group,
                 ClutterActor *child)
{
  IncrementalInfo *info;

  info = g_object_get_data(G_OBJECT (group), "incremental-info");
  if (!info)
    {
      info=incremental_init (group, 0, 0, 1000, 0);
    }
  gint width, height;

  clutter_group_add (group, child);

  width = clutter_actor_get_width (child);
  height = clutter_actor_get_height (child);

  if (info->x + width > info->width)
    {
      info->x = info->x0;
      info->y += info->max_height + info->padding_bottom;
      info->max_height = 0;
    }

  clutter_actor_set_position (child, info->x, info->y);

  info->x += width;

  if (height > info->max_height)
    info->max_height = height;

  clutter_actor_show (child);
}

#include <stdlib.h>
#include <stdarg.h>

static void
incremental_add_many (ClutterGroup *group,
                      ClutterActor *first_child,
                      ...)
{
  ClutterActor *child;
  va_list var_args;
  va_start (var_args, first_child);
  child = first_child;
  while (child)
    {
      incremental_add (group, child);
      child = va_arg (var_args, ClutterActor*);
    }
  va_end (var_args);
}

void action_previous (ClutterActor *label,
                      gpointer      userdata)
{
  start_demo (current_demo - 1);
}

void action_next     (ClutterActor *label,
                      gpointer      userdata)
{
  start_demo (current_demo + 1);
}

void action_quit        (ClutterActor *label,
                         gpointer      userdata)
{
  clutter_main_quit ();
}

void action_toggle_playing (ClutterActor *actor,
                            gpointer      userdata)
{
  playing = !playing;
  clutter_label_set_text (CLUTTER_LABEL (actor), playing?" playing":" paused");
  clutter_box2d_set_playing (CLUTTER_BOX2D (demos[current_demo].world), playing);
}

static void
stage_key_release_cb (ClutterStage           *stage,
                      ClutterKeyEvent        *kev,
                      gpointer                user_data)
{
  switch (clutter_key_event_symbol (kev))
    {
      case CLUTTER_q:    action_quit (NULL, NULL);    break;
      case CLUTTER_Left: action_previous (NULL,NULL); break;
      case CLUTTER_Right:action_next (NULL, NULL);    break;
      default:           action_next (NULL, NULL);    break;
    }
}


gint
main (int argc,
      char *argv[])
{
  ClutterActor *stage;
  ClutterColor  stage_color = { 0xcc, 0xee, 0xff, 0xff };

  clutter_init (&argc, &argv);

  stage = clutter_stage_get_default ();
  clutter_stage_set_color (CLUTTER_STAGE (stage), &stage_color);
  clutter_actor_set_size (stage, 512, 384);

  incremental_add_many (CLUTTER_GROUP (stage),
    label_action ("Sans 30px", "q ",      "black", action_quit , NULL),
    label_action ("Sans 30px", "←",   "black", action_previous, NULL),
    label_action ("Sans 30px", "→ ",   "black", action_next, NULL),
    label_action ("Sans 15px", " playing", "black", action_toggle_playing, NULL),
    NULL);

  start_demo (0);
  clutter_actor_show (stage);

  g_signal_connect (stage,
                    "key-release-event",
                    G_CALLBACK (stage_key_release_cb),
                    NULL);

  clutter_main ();

  return EXIT_SUCCESS;
}

void
start_demo (gint demo_no)
{
  g_print ("go to demo %i\n", demo_no);

  if (demo_no < 0 ||
      demo_no >= n_demos)
    {
      g_print ("accessed demo out of range, setting demo=0\n");
      demo_no = 0;
    }

  if (current_demo>=0)
    {
      if (demos[current_demo].destroy)
        demos[current_demo].destroy (&demos[current_demo]);
      else
        clutter_actor_destroy (demos[current_demo].world);
    }

  if (demos[demo_no].create)
    demos[demo_no].create (&demos[demo_no]);

  current_demo = demo_no;
}

/****************************************************************/



ClutterActor     *world;  /* ClutterBox2d group */

ClutterActor *active_actor = NULL;
gboolean in_drag = FALSE;
GdkPixbuf        *hand_pixbuf = NULL;



static gboolean actor_press (ClutterActor *actor,
                             ClutterEvent *event,
                             gpointer      data)
{
  active_actor = actor;
  clutter_actor_raise_top (actor);
  in_drag = TRUE;
  clutter_grab_pointer (actor);
  return FALSE;
}


static gboolean actor_release (ClutterActor *actor,
                               ClutterEvent *event,
                               gpointer      data)
{
  if (in_drag)
    {
      in_drag = FALSE;
      clutter_ungrab_pointer ();
    }
  return FALSE;
}

static gboolean actor_motion (ClutterActor *actor,
                              ClutterEvent *event,
                              gpointer      data)
{
  if (active_actor &&
      in_drag)
    {
      ClutterUnit x;
      ClutterUnit y;

      x = CLUTTER_UNITS_FROM_INT (event->button.x);
      y = CLUTTER_UNITS_FROM_INT (event->button.y);

      clutter_actor_transform_stage_point (
        clutter_actor_get_parent (active_actor),
        x, y,
        &x, &y);

      clutter_actor_set_positionu (active_actor, x, y);
    }
  return FALSE;
}

static void
make_actor_draggable (ClutterActor *actor)
{
  g_signal_connect (actor, "button-press-event",
                            G_CALLBACK (actor_press), NULL);
  g_signal_connect (actor, "button-release-event",
                            G_CALLBACK (actor_release), NULL);
  g_signal_connect (actor, "motion-event",
                            G_CALLBACK (actor_motion), NULL);
  clutter_actor_set_reactive (actor, TRUE);
}

static void
add_hand (gint          x,
          gint          y)
{
  ClutterActor *actor;
  actor = clutter_texture_new_from_pixbuf (hand_pixbuf);
  clutter_group_add (CLUTTER_GROUP (world), actor);

  clutter_actor_set_opacity (actor, 0.5*255);
  clutter_actor_set_position (actor, x, y);
  clutter_box2d_actor_set_type (CLUTTER_BOX2D (world), actor, CLUTTER_BOX2D_DYNAMIC);
  clutter_actor_show (actor);

  make_actor_draggable (actor);
}

static gboolean stage_press (ClutterActor *stage,
                             ClutterEvent *event,
                             gpointer      data)
{
  if (event->button.button == 3)
    {
      ClutterUnit x;
      ClutterUnit y;

      x = CLUTTER_UNITS_FROM_INT (event->button.x);
      y = CLUTTER_UNITS_FROM_INT (event->button.y);

      clutter_actor_transform_stage_point (
        world,
        x, y,
        &x, &y);

      add_hand (CLUTTER_UNITS_TO_INT (x),
                CLUTTER_UNITS_TO_INT (y));

      return FALSE;
    }

  if (active_actor != NULL)
    {
      ClutterUnit x;
      ClutterUnit y;

      x = CLUTTER_UNITS_FROM_INT (event->button.x);
      y = CLUTTER_UNITS_FROM_INT (event->button.y);

      clutter_actor_transform_stage_point (
        clutter_actor_get_parent (active_actor),
        x, y,
        &x, &y);

      clutter_actor_set_positionu (active_actor, x, y);
      in_drag = TRUE;
    }
  return FALSE;
}

void demo_pyramid (Demo *demo)
{
  ClutterActor     *stage;
  ClutterActor     *box;

  stage = clutter_stage_get_default ();

  world = g_object_new (CLUTTER_TYPE_BOX2D, NULL);
  clutter_group_add (CLUTTER_GROUP (stage), world);
  clutter_actor_show (world);
  demo->world = world;

  box = clutter_rectangle_new ();
  clutter_actor_set_size (box, 1424, 4);
  clutter_actor_set_position (box, -400, 600);
  clutter_group_add (CLUTTER_GROUP (world), box);
  clutter_box2d_actor_set_type (CLUTTER_BOX2D (world), box, CLUTTER_BOX2D_STATIC);
  clutter_actor_show (box);
  make_actor_draggable (box);

#define PYRAMID_ROWS 7
  {
    gint row;
    for (row = 0; row <= PYRAMID_ROWS; row ++)
      {
        gint x, y;
        gint i;
        y = 420 - (row+1) * 120;

        x = 160;
        add_hand (x, y);
        for (i=PYRAMID_ROWS-row; i > 0; i--)
          {
            x = 160 - i * 64;
            add_hand (x, y);
            x = 160 + i * 64;
            add_hand (x, y);
          }
      }
  }
  clutter_box2d_set_playing (CLUTTER_BOX2D (world), playing);
  clutter_actor_set_depth (world, -600);
}


void demo_slides (Demo *demo)
{
  ClutterActor     *ground;
  GdkPixbuf        *pixbuf;
  ClutterActor     *stage;
  gint i;
  GError           *error;

  stage = clutter_stage_get_default ();

  error = NULL;
  pixbuf = gdk_pixbuf_new_from_file (ASSETS_DIR "mars.png", &error);
  if (error)
    {
      g_warning ("Unable to load assets/mars.png: %s", error->message);
      ground = clutter_rectangle_new ();
      clutter_actor_set_size (ground, 500, 120);
      g_object_unref (pixbuf);
    }
  else
    {
      ground = clutter_texture_new_from_pixbuf (pixbuf);
      g_object_unref (pixbuf);
    }

  error = NULL;
  hand_pixbuf = gdk_pixbuf_new_from_file (ASSETS_DIR "redhand.png", &error);
  if (error)
    g_error ("Unable to load redhand.png: %s", error->message);


  world = g_object_new (CLUTTER_TYPE_BOX2D, NULL);
  clutter_group_add (CLUTTER_GROUP (stage), world);

  clutter_group_add (CLUTTER_GROUP (world), ground);
  clutter_actor_set_position (ground, clutter_actor_get_width(ground)*-0.3, 568); /* this is wrong */

  clutter_actor_show (ground);

  ground = clutter_rectangle_new ();
  clutter_actor_set_size (ground, 1024, 3);
  clutter_actor_set_position (ground, -300, 700);
  clutter_group_add (CLUTTER_GROUP (world), ground);
  clutter_box2d_actor_set_type (CLUTTER_BOX2D (world), ground, CLUTTER_BOX2D_STATIC);
  clutter_actor_show (ground);

  ground = clutter_rectangle_new ();
  clutter_actor_set_size (ground, 256, 3);
  clutter_actor_set_position (ground, -100, 310);
  clutter_actor_set_rotation (ground, CLUTTER_Z_AXIS, 30, 128,16,0);
  clutter_group_add (CLUTTER_GROUP (world), ground);
  clutter_box2d_actor_set_type (CLUTTER_BOX2D (world), ground, CLUTTER_BOX2D_STATIC);
  clutter_actor_show (ground);

  ground = clutter_rectangle_new ();
  clutter_actor_set_size (ground, 256, 3);
  clutter_actor_set_position (ground, 200, 200);
  clutter_actor_set_rotation (ground, CLUTTER_Z_AXIS, -30, 0,0,0);
  clutter_group_add (CLUTTER_GROUP (world), ground);
  clutter_box2d_actor_set_type (CLUTTER_BOX2D (world), ground, CLUTTER_BOX2D_STATIC);
  clutter_actor_show (ground);

  for (i=0; i< 20; i++)
    {
      add_hand (g_random_int_range (-100, 200),
                g_random_int_range (-800, 0));

    }
  clutter_actor_show (world);

  clutter_actor_set_depth (world, -600);
  clutter_actor_set_position (world, 0, -100);

  /*g_object_unref (pixbuf);*/

  if(1){
    g_signal_connect (world, "button-press-event",
                      G_CALLBACK (stage_press), NULL);
    g_signal_connect (world, "motion-event",
                      G_CALLBACK (actor_motion), NULL);
    g_signal_connect (world, "button-release-event",
                      G_CALLBACK (actor_release), NULL);
  }

  clutter_actor_set_reactive (world, TRUE);

  clutter_box2d_set_playing (CLUTTER_BOX2D (world), playing);

  demo->world = world;
}


void demo_control (Demo *demo)
{
  ClutterActor     *ground;
  ClutterActor     *stage;
  GError           *error;
  gint i;

  stage = clutter_stage_get_default ();

  error = NULL;
  if (hand_pixbuf==NULL)
    hand_pixbuf = gdk_pixbuf_new_from_file (ASSETS_DIR "redhand.png", &error);
  if (error)
    g_error ("Unable to load redhand.png: %s", error->message);

  world = g_object_new (CLUTTER_TYPE_BOX2D, NULL);
  clutter_group_add (CLUTTER_GROUP (stage), world);

  ground = clutter_rectangle_new ();
  clutter_actor_set_size (ground, 1024, 3);
  clutter_actor_set_position (ground, -300, 700);

  clutter_group_add (CLUTTER_GROUP (world), ground);
  /* actor needs to be added to the box2d group before
   * the type can be set.
   */
  clutter_box2d_actor_set_type (CLUTTER_BOX2D (world),
                                ground, CLUTTER_BOX2D_STATIC);
  clutter_actor_show (ground);

  ground = clutter_rectangle_new ();
  clutter_actor_set_size (ground, 256, 3);
  clutter_actor_set_position (ground, -100, 310);
  clutter_actor_set_rotation (ground, CLUTTER_Z_AXIS, 30, 128,16,0);
  clutter_group_add (CLUTTER_GROUP (world), ground);
  clutter_box2d_actor_set_type (CLUTTER_BOX2D (world), ground, CLUTTER_BOX2D_STATIC);
  clutter_actor_show (ground);

  ground = clutter_rectangle_new ();
  clutter_actor_set_size (ground, 256, 3);
  clutter_actor_set_position (ground, 200, 200);
  clutter_actor_set_rotation (ground, CLUTTER_Z_AXIS, -30, 0,0,0);
  clutter_group_add (CLUTTER_GROUP (world), ground);
  clutter_box2d_actor_set_type (CLUTTER_BOX2D (world), ground, CLUTTER_BOX2D_STATIC);
  clutter_actor_show (ground);

  for (i=0; i< 20; i++)
    {
      add_hand (g_random_int_range (-100, 200),
                g_random_int_range (-800, 0));

    }
  clutter_actor_show (world);

  clutter_actor_set_depth (world, -600);
  clutter_actor_set_position (world, 0, -100);

  if(1){
    g_signal_connect (world, "button-press-event",
                      G_CALLBACK (stage_press), NULL);
    g_signal_connect (world, "motion-event",
                      G_CALLBACK (actor_motion), NULL);
    g_signal_connect (world, "button-release-event",
                      G_CALLBACK (actor_release), NULL);
  }

  clutter_actor_set_reactive (world, TRUE);

  clutter_box2d_set_playing (CLUTTER_BOX2D (world), playing);

  demo->world = world;
}

void demo_about (Demo *demo)
{
  ClutterColor      white = {0xff, 0xff, 0xff, 0xff};
  ClutterActor     *ground;
  ClutterActor     *rectangle;
  ClutterActor     *title;
  ClutterActor     *stage;
  GError           *error;

  stage = clutter_stage_get_default ();

  error = NULL;
  if (hand_pixbuf==NULL)
    hand_pixbuf = gdk_pixbuf_new_from_file (ASSETS_DIR "redhand.png", &error);
  if (error)
    g_error ("Unable to load redhand.png: %s", error->message);

  world = g_object_new (CLUTTER_TYPE_BOX2D, NULL);
  clutter_actor_show (world);
  clutter_group_add (CLUTTER_GROUP (stage), world);

  title = clutter_label_new_full ("Sans 20px",
"This application is a collection\n"
"clutter+box2d experiments, activate\n"
"the arrows to change experiment.\n"
"\n"
"activate playing|pause to freeze.\n"
"simulation.", &white);

  clutter_actor_show (title);
  clutter_actor_set_position (title, 40, 40);
  clutter_group_add (CLUTTER_GROUP (world), title);


  ground = clutter_rectangle_new ();
  clutter_actor_show (ground);
  clutter_actor_set_size (ground, 1024, 5);
  clutter_actor_set_position (ground, -300, 350);
  clutter_group_add (CLUTTER_GROUP (world), ground);
  clutter_box2d_actor_set_type (CLUTTER_BOX2D (world),
                                ground, CLUTTER_BOX2D_STATIC);


  rectangle = clutter_rectangle_new ();
  clutter_actor_show (rectangle);
  clutter_actor_set_size (rectangle, 150, 150);
  clutter_actor_set_position (rectangle, 100, 120);
  clutter_actor_set_rotation (rectangle, CLUTTER_Z_AXIS, 23, 0,0,0);

  clutter_group_add (CLUTTER_GROUP (world), rectangle);
  clutter_box2d_actor_set_type (CLUTTER_BOX2D (world),
                                rectangle, CLUTTER_BOX2D_DYNAMIC);

  title = clutter_label_new_with_text ("Sans 40px", "Clutter-box2d");
  clutter_actor_show (title);
  clutter_actor_set_position (title, 100, 120);
  clutter_actor_set_rotation (title, CLUTTER_Z_AXIS, 23, 0,0,0);
  clutter_group_add (CLUTTER_GROUP (world), title);
  clutter_box2d_actor_set_type (CLUTTER_BOX2D (world),
                                title, CLUTTER_BOX2D_DYNAMIC);


  clutter_box2d_set_playing (CLUTTER_BOX2D (world), playing);

  demo->world = world;
}
