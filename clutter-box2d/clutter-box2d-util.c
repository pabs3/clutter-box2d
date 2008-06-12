/* clutter-box2d - Clutter box2d integration
 *
 * This file implements a the ClutterBox2DActor class which tracks the
 * physics simulation state of an actor. Every actor in a ClutterBox2D
 * container has an assoicated such object for synchronizing visual/physical state.
 *
 * Copyright 2008 OpenedHand Ltd
 * Authored by Øyvind Kolås <pippin@o-hand.com>
 * Licensed under the LGPL v2 or greater.
 */

#include <clutter/clutter.h>

typedef struct TrackData
{
  ClutterActor *self;
  ClutterActor *other;
  ClutterUnit   rel_x;
  ClutterUnit   rel_y;
  gint          x_handler;
  gint          y_handler;
} TrackData;


static void clutter_box2d_actor_track_update (ClutterActor *actor,
                                              GParamSpec   *pspec,
                                              gpointer      data)
{
  TrackData *td;
  ClutterUnit x, y;
  td = data;
  clutter_actor_get_positionu (td->other, &x, &y);
  clutter_actor_set_positionu (td->self, x + td->rel_x, y + td->rel_y);
}

/* 
 * Make an actor maintain the relative position to another actor, the position
 * of actor will change when notify events are emitted for the "x" or "y"
 * properties on other.
 *
 * Neither of the actors have to be children of a box2d group, but this is probably
 * most useful when "other" is a box2d controlled actor (that might be hidden) and
 * actor is a user visible ClutterActor.
 */
void clutter_box2d_actor_track (ClutterActor *actor,
                                ClutterActor *other)
{
  TrackData *td;
  td = g_object_get_data (G_OBJECT (actor), "track-data");
  if (!td)
    {
      td = g_new0 (TrackData, 1);
      g_object_set_data (G_OBJECT (actor), "track-data", td);
      td->self = actor;
    }

  if (td->x_handler)
    {
      g_signal_handler_disconnect (td->other, td->x_handler);
      td->x_handler = 0;
    }
  if (td->y_handler)
    {
      g_signal_handler_disconnect (td->other, td->y_handler);
      td->y_handler = 0;
    }
  if (!other)
    {
      return;
    }
  td->other = other;



  td->rel_x = clutter_actor_get_xu (actor) - clutter_actor_get_xu (other);
  td->rel_y = clutter_actor_get_yu (actor) - clutter_actor_get_yu (other);

  /* listen for notifies when the others position change and then change
   * the position of ourself accordingly.
   */
  td->x_handler = g_signal_connect (G_OBJECT (other), "notify::x",
                                  G_CALLBACK (clutter_box2d_actor_track_update), td);
  td->y_handler = g_signal_connect (G_OBJECT (other), "notify::y",
                                  G_CALLBACK (clutter_box2d_actor_track_update), td);
}