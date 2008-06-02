/* clutter-box2d - Clutter box2d integration
 *
 * This file implements a special ClutterGroup subclass that
 * allows simulating physical interactions of it's child actors
 * through the use of Box2D
 *
 * Copyright 2007, 2008 OpenedHand Ltd
 * Authored by Øyvind Kolås <pippin@o-hand.com>
 * Licensed under the LGPL v2 or greater.
 */

#define SCALE_FACTOR        0.05
#define INV_SCALE_FACTOR    (1.0/SCALE_FACTOR)

#define SYNCLOG(argv...)    if (0) g_print (argv)

#include "Box2D.h"
#include <clutter/clutter.h>
#include "clutter-box2d.h"
#include "clutter-box2d-actor.h"
#include "math.h"

static void clutter_container_iface_init (ClutterContainerIface *iface);

G_DEFINE_TYPE_WITH_CODE (ClutterBox2D, clutter_box2d, CLUTTER_TYPE_GROUP,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_CONTAINER,
                             clutter_container_iface_init));

#define CLUTTER_BOX2D_GET_PRIVATE(obj)                 \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), CLUTTER_TYPE_BOX2D, ClutterBox2DPrivate))

enum
{
  PROP_0,
  PROP_GRAVITY,
  PROP_SIMULATING
};

struct _ClutterBox2DPrivate
{
  gdouble          fps;    /* The framerate simulation is running at        */
  gint             iterations;/* number of engine iterations per processing */
  ClutterTimeline *timeline;  /* The timeline driving the simulation        */
};

typedef enum 
{
  CLUTTER_BOX2D_JOINT_DEAD,      /* An associated actor has been killed off */
  CLUTTER_BOX2D_JOINT_DISTANCE,
  CLUTTER_BOX2D_JOINT_PRISMATIC,
  CLUTTER_BOX2D_JOINT_REVOLUTE,
  CLUTTER_BOX2D_JOINT_MOUSE
} ClutterBox2DJointType;


/* A Box2DJoint contains all relevant tracking information
 * for a box2d joint.
 */
struct _ClutterBox2DJoint
{
  ClutterBox2D     *box2d;/* The ClutterBox2D this joint management struct
                             belongs to */
  ClutterBox2DJointType *type;  /* The type of joint */

  b2Joint               *joint; /* Box2d joint*/

  /* The actors hooked up to this joint, for a JOINT_MOUSE, only actor1 will
   * be set actor2 will be NULL
   */
  ClutterBox2DActor     *actor1;  
  ClutterBox2DActor     *actor2; 
};

static ClutterBox2DActor *
clutter_box2d_get_actor (ClutterBox2D   *box2d,
                         ClutterActor   *actor)
{
  return CLUTTER_BOX2D_ACTOR (clutter_container_get_child_meta (CLUTTER_CONTAINER (box2d), actor));
}


static GObject * clutter_box2d_constructor (GType                  type,
                                            guint                  n_params,
                                            GObjectConstructParam *params);
static void      clutter_box2d_dispose     (GObject               *object);

static void      clutter_box2d_iterate       (ClutterTimeline       *timeline,
                                              gint                   frame_num,
                                              gpointer               data);

static void
clutter_box2d_paint (ClutterActor *actor)
{
  CLUTTER_ACTOR_CLASS (clutter_box2d_parent_class)->paint (actor);

  /* XXX: enable drawing the shapes over the actors, as well as drawing
   *      lines for the joints
   */
}

void
clutter_box2d_set_gravity (ClutterBox2D        *box2d,
                           const ClutterVertex *gravity)
{
  b2Vec2 b2gravity = b2Vec2(CLUTTER_UNITS_TO_FLOAT (gravity->x),
                     CLUTTER_UNITS_TO_FLOAT (gravity->y));
     
  ((b2World*)box2d->world)->SetGravity (b2gravity); 
}

static void
clutter_box2d_set_property (GObject      *gobject,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  ClutterBox2D *box2d = CLUTTER_BOX2D (gobject);

  switch (prop_id)
    {
    case PROP_GRAVITY:
      {
        clutter_box2d_set_gravity (box2d,
                                   (ClutterVertex*)g_value_get_boxed (value));
      }
      break;
    case PROP_SIMULATING:
      {
        clutter_box2d_set_simulating (box2d, (gboolean)g_value_get_boolean (value));
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_box2d_get_property (GObject    *gobject,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  ClutterBox2D *box2d = CLUTTER_BOX2D (gobject);

  switch (prop_id)
    {
    /*case PROP_GRAVITY:
      {
        ClutterVertex gravity = {0, };

        clutter_box2d_get_gravity (box2d, &gravity);
        g_value_set_boxed (value, &gravity);
      }
      break;*/
    case PROP_SIMULATING:
        g_value_set_boolean (value,
                             clutter_box2d_get_simulating (box2d));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}


static void
clutter_box2d_class_init (ClutterBox2DClass *klass)
{
  GObjectClass          *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorClass     *actor_class   = CLUTTER_ACTOR_CLASS (klass);

  gobject_class->dispose      = clutter_box2d_dispose;
  gobject_class->constructor  = clutter_box2d_constructor;
  gobject_class->set_property = clutter_box2d_set_property;
  gobject_class->get_property = clutter_box2d_get_property;
  actor_class->paint          = clutter_box2d_paint;



  g_type_class_add_private (gobject_class, sizeof (ClutterBox2DPrivate));

  g_object_class_install_property (gobject_class,
                                   PROP_GRAVITY,
                                   g_param_spec_boxed ("gravity",
                                                       "Gravity",
                                                       "The gravity of ",
                                                       CLUTTER_TYPE_VERTEX,
                                                       G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class,
                                   PROP_SIMULATING,
                                   g_param_spec_boolean ("simulating",
                                                         "Simulatin",
                                                         "Whether ClutterBox2D is performing physical simulation or not.",
                                                         TRUE,
                                                         static_cast<GParamFlags>(G_PARAM_READWRITE)));

}

static void
clutter_box2d_init (ClutterBox2D *self)
{
  self->priv = CLUTTER_BOX2D_GET_PRIVATE (self);
}

ClutterActor *   clutter_box2d_new (void)
{
  return CLUTTER_ACTOR (g_object_new (CLUTTER_TYPE_BOX2D, NULL));
}

static GObject *
clutter_box2d_constructor (GType                  type,
                           guint                  n_params,
                           GObjectConstructParam *params)
{
  GObject              *object;
  ClutterBox2D         *self;
  ClutterBox2DPrivate  *priv;
  bool                  doSleep;

  b2AABB                worldAABB;

  /*  these magic numbers are the extent of the world,
   *  should be set large enough to contain most geometry,
   *  not sure how large this can be without impacting performance.
   */
  worldAABB.lowerBound.Set (-650.0f, -650.0f);
  worldAABB.upperBound.Set (650.0f, 650.0f);

  object = G_OBJECT_CLASS (clutter_box2d_parent_class)->constructor (
    type, n_params, params);

  self = CLUTTER_BOX2D (object);
  priv = CLUTTER_BOX2D_GET_PRIVATE (self);

  self->world = new b2World (worldAABB, /*gravity:*/ b2Vec2 (0.0f, 5.0f),
                             doSleep = false);

  priv->fps        = 25;
  priv->iterations = 50;

  self->actors = g_hash_table_new (g_direct_hash, g_direct_equal);
  self->bodies = g_hash_table_new (g_direct_hash, g_direct_equal);

  /* we make the timeline play continously to have a constant source
   * of new-frame events as long as the timeline is playing.
   */
  priv->timeline = clutter_timeline_new ((int) (priv->fps * 10),
                                         (int) priv->fps);
  g_object_set (priv->timeline, "loop", TRUE, NULL);
  g_signal_connect (priv->timeline, "new-frame",
                    G_CALLBACK (clutter_box2d_iterate), object);

  return object;
}

static void
clutter_box2d_dispose (GObject *object)
{
  ClutterBox2D        *self = CLUTTER_BOX2D (object);
  ClutterBox2DPrivate *priv = CLUTTER_BOX2D_GET_PRIVATE (self);

  G_OBJECT_CLASS (clutter_box2d_parent_class)->dispose (object);

  if (priv->timeline)
    {
      g_object_unref (priv->timeline);
      priv->timeline = NULL;
    }
  if (self->actors)
    {
      g_hash_table_destroy (self->actors);
      self->actors = NULL;
    }
  if (self->bodies)
    {
      g_hash_table_destroy (self->bodies);
      self->bodies = NULL;
    }
}


static void      clutter_box2d_create_child_meta (ClutterContainer *container,
                                                  ClutterActor     *actor)

{
  ClutterChildMeta  *child_meta;
  ClutterBox2DActor *box2d_actor;

  child_meta = CLUTTER_CHILD_META (g_object_new (CLUTTER_TYPE_BOX2D_ACTOR, NULL));

  child_meta->container = container;
  child_meta->actor = actor;

  box2d_actor = CLUTTER_BOX2D_ACTOR (child_meta);
  box2d_actor->world = (b2World*)(CLUTTER_BOX2D (container)->world);

  g_hash_table_insert (CLUTTER_BOX2D (container)->actors, actor, child_meta);
}

static void
clutter_box2d_destroy_child_meta (ClutterContainer *box2d,
                                  ClutterActor     *actor)
{
  ClutterBox2DActor *box2d_actor =
     CLUTTER_BOX2D_ACTOR (clutter_container_get_child_meta ( box2d, actor));

  g_assert (box2d_actor->world);

  g_hash_table_remove (CLUTTER_BOX2D (box2d)->actors, actor);
  g_hash_table_remove (CLUTTER_BOX2D (box2d)->bodies, box2d_actor->body);
}

static ClutterChildMeta *
clutter_box2d_get_child_meta (ClutterContainer *box2d,
                ClutterActor     *actor)
{
  return CLUTTER_CHILD_META (g_hash_table_lookup (CLUTTER_BOX2D (box2d)->actors, actor));
}

static void clutter_container_iface_init (ClutterContainerIface *iface)
{
  iface->child_meta_type = CLUTTER_TYPE_BOX2D_ACTOR;
  iface->create_child_meta = clutter_box2d_create_child_meta;
  iface->destroy_child_meta = clutter_box2d_destroy_child_meta;   
  iface->get_child_meta = clutter_box2d_get_child_meta;
}

/* make sure that the shape attached to the body matches the clutter realms
 * idea of the shape.
 */
static inline void
ensure_shape (ClutterBox2DActor *box2d_actor)
{
  if (box2d_actor->shape == NULL)
    {
      b2PolygonDef shapeDef;
      gint         width, height;
      gdouble      rot;

      width  = clutter_actor_get_width (CLUTTER_CHILD_META (box2d_actor)->actor);
      height = clutter_actor_get_height (CLUTTER_CHILD_META (box2d_actor)->actor);
      rot    = clutter_actor_get_rotation (CLUTTER_CHILD_META (box2d_actor)->actor,
                                           CLUTTER_Z_AXIS, NULL, NULL, NULL);


      shapeDef.SetAsBox (width * 0.5 * SCALE_FACTOR,
                         height * 0.5 * SCALE_FACTOR,
                         b2Vec2 (width * 0.5 * SCALE_FACTOR,
                         height * 0.5 * SCALE_FACTOR), 0);
      shapeDef.density   = 10.0f;
      shapeDef.friction = 0.2f;
      box2d_actor->shape = box2d_actor->body->CreateShape (&shapeDef);
    }
  else
    {
      /*XXX: recreate shape on every ensure? */
    }
}

/* Set the type of physical object an actor in a Box2D group is of.
 */
void
clutter_box2d_actor_set_type (ClutterBox2D      *box2d,
                              ClutterActor      *actor,
                              ClutterBox2DType   type)
{
  ClutterBox2DActor   *box2d_actor = CLUTTER_BOX2D_ACTOR (clutter_container_get_child_meta (
     CLUTTER_CONTAINER (box2d), actor));

  if (box2d_actor->type == type)
    return;

  if (box2d_actor->type != CLUTTER_BOX2D_NONE)
    {
      g_assert (box2d_actor->body);

      g_hash_table_remove (box2d->bodies, box2d_actor->body);
      box2d_actor->world->DestroyBody (box2d_actor->body);
      box2d_actor->body = NULL;
      box2d_actor->shape = NULL;
      box2d_actor->type = CLUTTER_BOX2D_NONE;
    }

  if (type == CLUTTER_BOX2D_DYNAMIC ||
      type == CLUTTER_BOX2D_STATIC)
    {
      b2BodyDef bodyDef;

      bodyDef.linearDamping = 0.0f;
      bodyDef.angularDamping = 0.02f;

      SYNCLOG ("making an actor to be %s\n",
               type == CLUTTER_BOX2D_STATIC ? "static" : "dynamic");
      
      box2d_actor->type = type;

      if (type == CLUTTER_BOX2D_DYNAMIC)
        {
          box2d_actor->body = box2d_actor->world->CreateDynamicBody (&bodyDef);
        }
      else if (type == CLUTTER_BOX2D_STATIC)
        {
          box2d_actor->body = box2d_actor->world->CreateStaticBody (&bodyDef);
        }
      sync_body (box2d_actor);
      box2d_actor->body->SetMassFromShapes ();
    }
  g_hash_table_insert (box2d->bodies, box2d_actor->body, box2d_actor);
}

/* Synchronise the state of the Box2D body with the
 * current geomery of the actor, only really do it if
 * we differ more than a certain delta to avoid disturbing
 * the physics computation
 */
void
sync_body (ClutterBox2DActor *box2d_actor)
{
  gint x, y;
  gdouble rot;

  ClutterActor *actor = CLUTTER_CHILD_META (box2d_actor)->actor;
  b2Body       *body  = box2d_actor->body;

  if (!body)
    return;

  rot = clutter_actor_get_rotation (CLUTTER_CHILD_META (box2d_actor)->actor,
                                    CLUTTER_Z_AXIS, NULL, NULL, NULL);


  x = clutter_actor_get_x (actor);
  y = clutter_actor_get_y (actor);


  b2Vec2 position = body->GetPosition ();

  if (fabs (x * SCALE_FACTOR - (position.x)) > 0.1 ||
      fabs (y * SCALE_FACTOR - (position.y)) > 0.1 ||
      fabs (body->GetAngle()*(180/3.1415) - rot) > 2.0
      )
    {
      ensure_shape (box2d_actor);
      body->SetXForm (b2Vec2 (x * SCALE_FACTOR, y * SCALE_FACTOR),
                      rot / (180 / 3.1415));

      SYNCLOG ("\t setxform: %d, %d, %f\n", x, y, rot);
    }
}

/* Synchronise actor geometry from body, rounding erorrs introduced here should
 * be smaller than the threshold accepted when sync_body is being run to avoid
 * introducing errors.
 */
void
sync_actor (ClutterBox2DActor *box2d_actor)
{
  ClutterActor *actor = CLUTTER_CHILD_META (box2d_actor)->actor;
  b2Body       *body  = box2d_actor->body;

  if (!body)
    return;

  clutter_actor_set_positionu (actor,
       CLUTTER_UNITS_FROM_FLOAT (body->GetPosition ().x * INV_SCALE_FACTOR),
       CLUTTER_UNITS_FROM_FLOAT (body->GetPosition ().y * INV_SCALE_FACTOR));

  SYNCLOG ("setting actor position: ' %f %f angle: %f\n",
           body->GetPosition ().x,
           body->GetPosition ().y,
           body->GetAngle () * (180 / 3.1415));

  clutter_actor_set_rotation (actor, CLUTTER_Z_AXIS,
                              body->GetAngle () * (180 / 3.1415),
                              0, 0, 0);
}

static void
clutter_box2d_iterate (ClutterTimeline *timeline,
                       gint             frame_num,
                       gpointer         data)
{
  ClutterBox2D *box2d = CLUTTER_BOX2D (data);
  guint         msecs;

  clutter_timeline_get_delta (timeline, &msecs);
  ClutterBox2DPrivate *priv = CLUTTER_BOX2D_GET_PRIVATE (box2d);
  gint                 steps = priv->iterations;
  b2World             *world = (b2World*)(box2d->world);

  {
    GList *actors = g_hash_table_get_values (box2d->actors);
    GList *iter;

    /* First we check for each actor the need for, and perform a sync
     * from the actor to the body before running simulation
     */
    for (iter = actors; iter; iter = g_list_next (iter))
      {
        ClutterBox2DActor *box2d_actor = (ClutterBox2DActor*) iter->data;
        sync_body (box2d_actor);
      }

    if (msecs == 0)
      return;

    /* Iterate Box2D simulation of bodies */
    world->Step (msecs / 1000.0, steps);


    /* syncronize actor to have geometrical sync with bodies */
    for (iter = actors; iter; iter = g_list_next (iter))
      {
        ClutterBox2DActor *box2d_actor = (ClutterBox2DActor*) iter->data;
        sync_actor (box2d_actor);
      }
    g_list_free (actors);
  }

}

void
clutter_box2d_set_simulating (ClutterBox2D  *box2d,
                              gboolean       simulating)
{
  ClutterBox2DPrivate *priv = CLUTTER_BOX2D_GET_PRIVATE (box2d);

  if (simulating)
    {
      clutter_timeline_start (priv->timeline);
    }
  else
    {
      clutter_timeline_stop (priv->timeline);
    }
}

gboolean
clutter_box2d_get_simulating (ClutterBox2D *box2d)
{
  ClutterBox2DPrivate *priv = CLUTTER_BOX2D_GET_PRIVATE (box2d);

  return clutter_timeline_is_playing (priv->timeline);
}

void *
clutter_box2d_actor_get_body (ClutterBox2D *box2d,
                              ClutterActor *actor)
{
  ClutterBox2DActor *box2d_actor = clutter_box2d_get_actor (box2d, actor);
  return box2d_actor->body;
}


void
clutter_box2d_actor_set_linear_velocity (ClutterBox2D        *box2d,
                                         ClutterActor        *actor,
                                         const ClutterVertex *linear_velocity)
{
  ClutterBox2DActor *box2d_actor = clutter_box2d_get_actor (box2d, actor);
  b2Vec2 b2velocity (CLUTTER_UNITS_TO_FLOAT (linear_velocity->x) * SCALE_FACTOR,
                     CLUTTER_UNITS_TO_FLOAT (linear_velocity->y) * SCALE_FACTOR);

  box2d_actor->body->SetLinearVelocity (b2velocity);
}

void
clutter_box2d_actor_get_linear_velocity (ClutterBox2D  *box2d,
                                         ClutterActor  *actor,
                                         ClutterVertex *linear_velocity)
{
  ClutterBox2DActor *box2d_actor = clutter_box2d_get_actor (box2d, actor);
  b2Vec2 b2velocity;
  
  b2velocity = box2d_actor->body->GetLinearVelocity();
  linear_velocity->x = CLUTTER_UNITS_FROM_FLOAT (
                               b2velocity.x * INV_SCALE_FACTOR);
  linear_velocity->y = CLUTTER_UNITS_FROM_FLOAT (
                               b2velocity.y * INV_SCALE_FACTOR);
}


void
clutter_box2d_actor_set_angular_velocity (ClutterBox2D *box2d,
                                          ClutterActor *actor,
                                          gdouble       omega)
{
  ClutterBox2DActor *box2d_actor = clutter_box2d_get_actor (box2d, actor);
  box2d_actor->body->SetAngularVelocity (omega);
}

gdouble
clutter_box2d_actor_get_angular_velocity (ClutterBox2D *box2d,
                                          ClutterActor *actor)
{
  ClutterBox2DActor *box2d_actor = clutter_box2d_get_actor (box2d, actor);
  return box2d_actor->body->GetAngularVelocity ();
}


void
clutter_box2d_actor_apply_force (ClutterBox2D     *box2d,
                                 ClutterActor     *actor,
                                 ClutterVertex    *force,
                                 ClutterVertex    *position)
{
  ClutterBox2DActor *box2d_actor = clutter_box2d_get_actor (box2d, actor);
  b2Vec2 b2force (CLUTTER_UNITS_TO_FLOAT (force->x) * SCALE_FACTOR,
                  CLUTTER_UNITS_TO_FLOAT (force->y) * SCALE_FACTOR);
  b2Vec2 b2position (CLUTTER_UNITS_TO_FLOAT (position->x) * SCALE_FACTOR,
                     CLUTTER_UNITS_TO_FLOAT (position->y) * SCALE_FACTOR);

  box2d_actor->body->ApplyForce (
           box2d_actor->body->GetWorldVector(b2force),
           box2d_actor->body->GetWorldVector(b2position));
}

void
clutter_box2d_actor_apply_impulse (ClutterBox2D     *box2d,
                                   ClutterActor     *actor,
                                   ClutterVertex    *force,
                                   ClutterVertex    *position)
{
  ClutterBox2DActor *box2d_actor = clutter_box2d_get_actor (box2d, actor);
  b2Vec2 b2force (CLUTTER_UNITS_TO_FLOAT (force->x) * SCALE_FACTOR,
                  CLUTTER_UNITS_TO_FLOAT (force->y) * SCALE_FACTOR);
  b2Vec2 b2position (CLUTTER_UNITS_TO_FLOAT (position->x) * SCALE_FACTOR,
                     CLUTTER_UNITS_TO_FLOAT (position->y) * SCALE_FACTOR);

  box2d_actor->body->ApplyImpulse (
           box2d_actor->body->GetWorldVector(b2force),
           box2d_actor->body->GetWorldVector(b2position));
}

void clutter_box2d_actor_apply_torque        (ClutterBox2D     *box2d,
                                              ClutterActor     *actor,
                                              gdouble           torque)
{
  ClutterBox2DActor *box2d_actor = clutter_box2d_get_actor (box2d, actor);
  box2d_actor->body->ApplyTorque (torque);
}



void * clutter_box2d_get_world      (ClutterBox2D *box2d)
{
  return box2d->world;
}

ClutterBox2DJoint *
clutter_box2d_add_joint (ClutterBox2D     *box2d,
                         ClutterActor     *actor_a,
                         ClutterActor     *actor_b,
                         gdouble           x_a,
                         gdouble           y_a,
                         gdouble           x_b,
                         gdouble           y_b,
                         gdouble           min_len,
                         gdouble           max_len)
{
  /*ClutterBox2DPrivate *priv = CLUTTER_BOX2D_GET_PRIVATE (box2d);*/
  b2RevoluteJointDef jd;
  /*b2DistanceJointDef jd;*/
  /*b2PrismaticJointDef jd;*/
  b2Vec2 anchor (x_a, y_a);

  /*jd.body1 = clutter_box2d_get_actor (box2d, actor_a)->body;
  jd.body2 = clutter_box2d_get_actor (box2d, actor_b)->body;*/
  /*jd.localAnchor1.Set(x_a, y_a);
  jd.localAnchor2.Set(x_b, y_b);*/
  /*jd.length = min_len;
  jd.lowerTranslation = min_len;
  jd.upperTranslation = max_len;*/
  /*jd.enableLimit = true;*/
  jd.collideConnected = false;
  jd.Initialize(clutter_box2d_get_actor (box2d, actor_a)->body,
                clutter_box2d_get_actor (box2d, actor_b)->body,
                anchor);
  ((b2World*)(box2d->world))->CreateJoint (&jd);

  return NULL;
}

static ClutterBox2DJoint *
joint_new (ClutterBox2D *box2d,
           b2Joint      *joint)
{
   /*ClutterBox2DPrivate *priv = CLUTTER_BOX2D_GET_PRIVATE (box2d);*/
   ClutterBox2DJoint *self = g_new0 (ClutterBox2DJoint, 1);
   self->box2d = box2d;
   self->joint = joint;

   self->actor1 = (ClutterBox2DActor*)
        g_hash_table_lookup (box2d->bodies, joint->GetBody1());
   if (self->actor1)
     {
        self->actor1->joints = g_list_append (self->actor1->joints, self);
     }
   self->actor2 = (ClutterBox2DActor*)
        g_hash_table_lookup (box2d->bodies, joint->GetBody2());
   if (self->actor2)
     {
        self->actor2->joints = g_list_append (self->actor2->joints, self);
     }

   return self;
}

void
clutter_box2d_joint_destroy (ClutterBox2DJoint *joint)
{
  ((b2World*)joint->box2d->world)->DestroyJoint (joint->joint);

   if (joint->actor1)
     {
        joint->actor1->joints = g_list_remove (joint->actor1->joints, joint);
     }
   if (joint->actor2)
     {
        joint->actor2->joints = g_list_remove (joint->actor2->joints, joint);
     }

  g_free (joint);
}


ClutterBox2DJoint *
clutter_box2d_add_distance_joint (ClutterBox2D        *box2d,
                                  ClutterActor        *actor1,
                                  ClutterActor        *actor2,
                                  const ClutterVertex *anchor1,
                                  const ClutterVertex *anchor2,
                                  gdouble              length,
                                  gdouble              frequency,
                                  gdouble              damping_ratio)
{
  /*ClutterBox2DPrivate *priv = CLUTTER_BOX2D_GET_PRIVATE (box2d);*/
  b2DistanceJointDef jd;

  jd.collideConnected = false;
  jd.body1 = clutter_box2d_get_actor (box2d, actor1)->body;
  jd.body2 = clutter_box2d_get_actor (box2d, actor2)->body;
  jd.localAnchor1 = b2Vec2(CLUTTER_UNITS_TO_FLOAT (anchor1->x) * SCALE_FACTOR,
                           CLUTTER_UNITS_TO_FLOAT (anchor1->y) * SCALE_FACTOR);
  jd.localAnchor2 = b2Vec2(CLUTTER_UNITS_TO_FLOAT (anchor2->x) * SCALE_FACTOR,
                           CLUTTER_UNITS_TO_FLOAT (anchor2->y) * SCALE_FACTOR);
  jd.length = length * SCALE_FACTOR;
  jd.frequencyHz = 0.0;
  jd.dampingRatio = 0.0;

  return joint_new (box2d, ((b2World*)box2d->world)->CreateJoint (&jd));
}


ClutterBox2DJoint *
clutter_box2d_add_distance_joint2 (ClutterBox2D        *box2d,
                                   ClutterActor        *actor1,
                                   ClutterActor        *actor2,
                                   const ClutterVertex *anchor1,
                                   const ClutterVertex *anchor2,
                                   gdouble              frequency,
                                   gdouble              damping_ratio)
{
  /* this one should compute the length automatically based on the
   * initial configuration?
   */
  return NULL;
}


ClutterBox2DJoint *
clutter_box2d_add_revolute_joint (ClutterBox2D        *box2d,
                                  ClutterActor        *actor1,
                                  ClutterActor        *actor2,
                                  const ClutterVertex *anchor1,
                                  const ClutterVertex *anchor2,
                                  gdouble              reference_angle)
{
  /*ClutterBox2DPrivate *priv = CLUTTER_BOX2D_GET_PRIVATE (box2d);*/
  b2RevoluteJointDef jd;

  jd.collideConnected = false;
  jd.body1 = clutter_box2d_get_actor (box2d, actor1)->body;
  jd.body2 = clutter_box2d_get_actor (box2d, actor2)->body;
  jd.localAnchor1 = b2Vec2(CLUTTER_UNITS_TO_FLOAT (anchor1->x) * SCALE_FACTOR,
                           CLUTTER_UNITS_TO_FLOAT (anchor1->y) * SCALE_FACTOR);
  jd.localAnchor2 = b2Vec2(CLUTTER_UNITS_TO_FLOAT (anchor2->x) * SCALE_FACTOR,
                           CLUTTER_UNITS_TO_FLOAT (anchor2->y) * SCALE_FACTOR);
  jd.referenceAngle = reference_angle;

  return joint_new (box2d, ((b2World*)box2d->world)->CreateJoint (&jd));
}

ClutterBox2DJoint *
clutter_box2d_add_revolute_joint2 (ClutterBox2D        *box2d,
                                   ClutterActor        *actor1,
                                   ClutterActor        *actor2,
                                   const ClutterVertex *anchor)
{
  /*ClutterBox2DPrivate *priv = CLUTTER_BOX2D_GET_PRIVATE (box2d);*/
  b2RevoluteJointDef jd;
  b2Vec2 ancho  (CLUTTER_UNITS_TO_FLOAT (anchor->x) * SCALE_FACTOR,
                 CLUTTER_UNITS_TO_FLOAT (anchor->y) * SCALE_FACTOR);

  jd.collideConnected = false;
  jd.Initialize(clutter_box2d_get_actor (box2d, actor1)->body,
                clutter_box2d_get_actor (box2d, actor2)->body,
                ancho);
  return joint_new (box2d, ((b2World*)box2d->world)->CreateJoint (&jd));
}

ClutterBox2DJoint *
clutter_box2d_add_prismatic_joint (ClutterBox2D        *box2d,
                                   ClutterActor        *actor1,
                                   ClutterActor        *actor2,
                                   const ClutterVertex *anchor1,
                                   const ClutterVertex *anchor2,
                                   gdouble              min_length,
                                   gdouble              max_length,
                                   const ClutterVertex *axis)
{
  /*ClutterBox2DPrivate *priv = CLUTTER_BOX2D_GET_PRIVATE (box2d);*/
  b2PrismaticJointDef jd;

  jd.collideConnected = false;
  jd.body1 = clutter_box2d_get_actor (box2d, actor1)->body;
  jd.body2 = clutter_box2d_get_actor (box2d, actor2)->body;
  jd.localAnchor1 = b2Vec2(CLUTTER_UNITS_TO_FLOAT (anchor1->x) * SCALE_FACTOR,
                           CLUTTER_UNITS_TO_FLOAT (anchor1->y) * SCALE_FACTOR);
  jd.localAnchor2 = b2Vec2(CLUTTER_UNITS_TO_FLOAT (anchor2->x) * SCALE_FACTOR,
                           CLUTTER_UNITS_TO_FLOAT (anchor2->y) * SCALE_FACTOR);
  jd.lowerTranslation = min_length * SCALE_FACTOR;
  jd.lowerTranslation = max_length * SCALE_FACTOR;
  jd.enableLimit = true;
  jd.localAxis1 = b2Vec2(CLUTTER_UNITS_TO_FLOAT (axis->x),
                         CLUTTER_UNITS_TO_FLOAT (axis->y));

  return joint_new (box2d, ((b2World*)box2d->world)->CreateJoint (&jd));
}


ClutterBox2DJoint *
clutter_box2d_add_mouse_joint (ClutterBox2D  *box2d,
                               ClutterActor  *actor,
                               ClutterVertex *target)
{
  /*ClutterBox2DPrivate *priv = CLUTTER_BOX2D_GET_PRIVATE (box2d);*/
  b2MouseJointDef md;

  md.body1 = ((b2World*)box2d->world)->GetGroundBody();
  md.body2 = clutter_box2d_get_actor (box2d, actor)->body;
  md.target = b2Vec2(CLUTTER_UNITS_TO_FLOAT (target->x) * SCALE_FACTOR,
                     CLUTTER_UNITS_TO_FLOAT (target->y) * SCALE_FACTOR);
  md.body1->WakeUp ();
  md.maxForce = 5100.0f * md.body2->GetMass ();

  return joint_new (box2d, ((b2World*)box2d->world)->CreateJoint(&md));
}

void
clutter_box2d_mouse_joint_update_target (ClutterBox2DJoint   *joint,
                                         const ClutterVertex *target)
{
  b2Vec2 b2target = b2Vec2(CLUTTER_UNITS_TO_FLOAT (target->x) * SCALE_FACTOR,
                           CLUTTER_UNITS_TO_FLOAT (target->y) * SCALE_FACTOR);
  static_cast<b2MouseJoint*>(joint->joint)->SetTarget(b2target);
}
