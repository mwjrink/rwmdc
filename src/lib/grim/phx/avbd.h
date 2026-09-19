#pragma once

// #include "lib/grim/gfx/graphics.h"
#include <lib/grim/assert.h>
#include <lib/grim/bp.h>
#include <lib/grim/math.h>
#include <lib/grim/mem/arena.h>
#include <lib/grim/mem/buff.h>
#include <stddef.h>

/*

How much cheaper is computing the sphere collision than the AABB collision?

sizeof(sphere_bb) == (4 * 4) == 16 == 64 / 4
bool Sphere(a, b) {
    // f32 sq_dst = ||a.center - b.center||;

    f32 x = (a.center.x - b.center.x);
    f32 y = (a.center.y - b.center.y);
    f32 z = (a.center.z - b.center.z);

    f32 sq_dst = x * x + y * y + z * z;

    // a^2 = (b^2 + c^2)
    // a < b + c
    // a^2 < b^2 + c^2 + bc

    // Not sure if storing the sq_rad is worth it. float mul is 3 or 4 cycle latency,
    // 0.5 throughput (2 at once) and 1 port for alder lake p or zen4 respectively
    if (sq_dst <= (a.rad * a.rad + b.rad * b.rad + a.rad * b.rad)) {
        return true;
    }

    return false;
}

sizeof(aabb) == (4 * 6) == 24 == 64 / 2
bool AABB(a, b) {
    if (
            (a.max_x > b.min_x && a.min_x < b.max_x) &&
            (a.max_y > b.min_y && a.min_y < b.max_y) &&
            (a.max_z > b.min_z && a.min_z < b.max_z)
        ) {
        return true;
    }

    return false;
}

// IMPORTANT CONCLUSION: Sphere is both computationally faster in terms of instructions but the memory footprint is the
   bottleneck. Sphere is smaller and you VERY quickly run into memory bandwidth limitations making spheres 50% faster
   due to being 50% smaller.
*/

/*
X Y Z X SAP to split islands

island centroid (x center, y center, z center) calculated
during SAP is origin for each island to avoid floating point
issues at 120KM from origin.

split islands into work stealing queue

per island:
- iterate over the elements and create transient constraints
- while creating transient constraints, calculate total system energy
- energy is directional per object and can be transferred through constraints/joints
- any energy going into a static is simply lost. We are done when energy is 0 (do we
  need a loop limit)?
- a 0 joint (hard position & rotation) transfers all energy 100%
    - what if I have A <-0-> B <-0-> C? Who gets the energy? or do we just consider
      it one object? IE transfer from A to B is not a valid question. that means
      during constraint solving, all constraints on any of A B or C are merged.
    - what about 0 position but 1.0 rotation? It's not a single object. Can't
      realistically evaluate rotation & velocity separately. Speculative energy?
      What about instead of giving energy, you only take energy? that way you can
      chain from C to B to A to find energy to take? does this allow negative energy
      on intermediary iterations, they just have to resolve to 0 by the end? ie a
      velocity on a mass is not 0 but we're measuring potential, not real energy.

just use avbd? This seems like essentially that without the math?

for raycasts, does ray intersect each island? If yes, sort islands by contact time t and
check everything in each island, move to the next if no hits.

island switching makes this super annoying/weird. it makes the multithreading really unclean.
how do you coordinate it? how do I even decide where and when to switch it? do I send it over
a channel? kinda gross.

graph coloring does work but also... ugh

if I'm graph coloring (which I'm not commited to) do I even bother with SAP? SAP should be fast
it would also allow me to color each island independently.

at that point... why am I bothering with coloring? What does it give me?

*/

typedef struct PhysicsHandle {
    u32 idx;
    u32 generation; // TODO could shrink this to even a single byte would be fine. slot generation, not system.
    // u16 generation;
    // struct {
    //     u8  is_static : 1;
    //     u16 _padding  : 15;
    // };
} PhysicsHandle;
STATIC_ASSERT(sizeof(PhysicsHandle) == 8);

typedef struct Transform {
    Vec3   pos;
    Rotor3 rotation;
} Transform;

typedef struct PhysicsComponent {
    u32 generation;

    union {
        Transform transform;
        struct {
            Vec3   position;
            Rotor3 rotation;
        };
    };

    union {
        Transform transform_dt;
        struct {
            Vec3   velocity;
            Rotor3 angular_velocity; // normal of rotation, magnitude is speed
        };
    };

    union {
        f32 mass;
        u32 next_empty;
    };
    // ModelUbo* model_ubo; // TODO need a way to associate these together, generation indices? A lookup of some sort?
    // Collider  collider;
} PhysicsComponent;
STATIC_ASSERT(sizeof(PhysicsComponent) == 64);

typedef struct PhysicsContext {
    // Why? Why not just use the normal arena and scratch as needed?
    // Arena physics_arena;

    // TODO physics arena
    // physics_components
    // when a component slot is empty, the generation is 0, idx points to the next empty entity
    // on delete, set gen = 0 and idx = last_free, last_free = delete_idx
    //     what if you on the fly sorted them to be lowest idx to highest? Every frame, just iterate through the
    //     components and skip any with gen 0?
    //
    // TODO measure the fragmentation, how bad would it be? My assumption is never that bad at all.
} PhysicsContext;
STATIC_ASSERT(sizeof(PhysicsContext) == 0);

static const u32 MAX_PHYSICS_COMPONENTS = 1024; // TODO much higher

typedef struct CollisionMesh {
    u32  idx;
    Vec3 center;
    Vec3 size; // TODO for now everything is a cube
    // f32  mass;
} CollisionMesh;
STATIC_ASSERT(sizeof(CollisionMesh) == 28);

typedef struct PhysicsConstraint {
    u32 idx_A;
    u32 idx_B;  // Always FROM A to B dir

    Vec3 dir;   // distance until application?
                // you do NOT need capacity. just move the resultant object
                // capacity is needed for a rope that will break under >= x impulse

    Vec3   offset;
    Rotor3 rot; // rotors loop at 360, need something else here. Gemini says use a vector and just add them
                // where each component is rotation around one of the standard axis
    f32    dt_consumed;
} PhysicsConstraint;
STATIC_ASSERT(sizeof(PhysicsConstraint) == 52);

typedef struct PhysicsState {
    // Collider* colliders; // TODO use these bare structs to calculate collisions but store idx/key in them.
    //                      // use the idx/key to create the collision constraints
    // u32        colliders_count;

    PhysicsComponent* components;
    CollisionMesh*    collision_meshes;
    u32               colliders_count;
    u32               components_count;
    u32               first_free_component_slot_idx;
    u32               _padding;
} PhysicsState;
STATIC_ASSERT(sizeof(PhysicsState) == 32);

PhysicsContext physics_context_create(rop(rw Arena) arena) {
    return (PhysicsContext){
        //
    };
}

PhysicsState physics_state_create(rop(rw Arena) arena, rop(rw PhysicsContext) p_ctx) {
    PhysicsComponent* components = arena_alloc_aligned(arena, typeof(PhysicsComponent), MAX_PHYSICS_COMPONENTS);

    return (PhysicsState){
        .components                    = components,
        .components_count              = 0,
        .first_free_component_slot_idx = 0,
    };
}

/*
   What is the minimum set of operations to get this to work?
   1. set the draw commands, the TRS (if they changed)
   2. calculate new TRS using physics, animations

   I think recreating the draw buffer per frame is fine? The statics part can be reused or just copied
   from a mostly static/unchanging buffer. the rest is just written over again, reuse the same buffer?

   Open Questions:
   1. does this do cloth or we put that on gpu? on gpu means no cloth hammock/parachute etc
        can be affected by forces, cannot apply a force
      A: Cloth on cpu
   2.

*/
void physics_update(rwp(rw ScratchArena) scratch, rwp(rw PhysicsState) state, f32 dt) {
    PhysicsConstraint* constraints       = scratch_dyn_start_align(scratch, alignof(PhysicsConstraint));
    PhysicsConstraint* constraints_start = constraints;

    // TODO should all constraints just be split on creation into x, y, z?
    // why make constraints at all then? just accumulate & cancel impulses/energy
    // on the spot in each axis?
    // - The only concern here, collision with ground = INF energy up. At the end, we'd
    //   end up with INF speed up. What about potential energy/potential limit/counteract?
    // - ALSO how do you deal with stack of blocks? You put force on one, split in half to
    //   the next, you have that split in half infinite times paradox. You also have to split
    //   the energy based on the mass ratio
    //
    // How does the constraint shit manage this?
    // This is Minimum Cost Flow Problem. Constraints are Verts, Edges are objects. You just minimize & split energy
    // around? Not sure how this handles pushing on one box in a line of boxes & splitting amongst them based on
    // friction, mass etc.

    // Collision detection
    for (u32 idx = 0; idx < state->colliders_count; idx++) {
        Vec3 center = state->collision_meshes[idx].center;
        Vec3 size   = state->collision_meshes[idx].size;

        PhysicsConstraint constraint = {
            .idx_A = 0,
            .idx_B = state->collision_meshes[idx].idx,
            .dir   = (Vec3){.y = f32_INF}, // infinite energy, assume static floor
        };

        Vec3 v0 = center;
        v0.x += size.x * 0.5f;
        v0.z += size.z * 0.5f;
        v0.y += size.y * 0.5f;

        constraint.offset = vec3_sub(v0, center);
        *constraints      = constraint;
        constraints += (v0.y <= f32_EPSILON);

        Vec3 v1 = v0;
        v1.x -= size.x;

        constraint.offset = vec3_sub(v1, center);
        *constraints      = constraint;
        constraints += (v0.y <= f32_EPSILON);

        Vec3 v2 = v1;
        v2.z -= size.z;

        constraint.offset = vec3_sub(v2, center);
        *constraints      = constraint;
        constraints += (v0.y <= f32_EPSILON);

        Vec3 v3 = v2;
        v3.x += size.x;

        constraint.offset = vec3_sub(v3, center);
        *constraints      = constraint;
        constraints += (v0.y <= f32_EPSILON);

        Vec3 v4 = v0;
        v4.y -= size.y;

        constraint.offset = vec3_sub(v4, center);
        *constraints      = constraint;
        constraints += (v0.y <= f32_EPSILON);

        Vec3 v5 = v4;
        v5.x -= size.x;

        constraint.offset = vec3_sub(v5, center);
        *constraints      = constraint;
        constraints += (v0.y <= f32_EPSILON);

        Vec3 v6 = v5;
        v6.z -= size.z;

        constraint.offset = vec3_sub(v6, center);
        *constraints      = constraint;
        constraints += (v0.y <= f32_EPSILON);

        Vec3 v7 = v6;
        v7.x += size.x;

        constraint.offset = vec3_sub(v7, center);
        *constraints      = constraint;
        constraints += (v0.y <= f32_EPSILON);

        // gravity
        constraint.offset = (Vec3){0};
        constraint.dir =
            (Vec3){.y = -9.81f * -9.81f * dt * dt * state->components[state->collision_meshes[idx].idx].mass};
        *constraints = constraint;
        constraints += (v0.y <= f32_EPSILON);

        // collider.handle -> component -> position?
    }

    // velocity -> constraint
    for (u32 idx = 0; idx < state->components_count; idx++) {
        PhysicsComponent component = state->components[idx];

        // TODO branch is def worth it here
        // should this be impulse?
        Vec3   energy_dir = vec3_mul_f32(vec3_mul_ew(component.velocity, component.velocity), component.mass);
        // TODO this is impulse
        Rotor3 energy_rot =
            rotor3_mul_f32(rotor3_mul_f32(component.angular_velocity, component.angular_velocity.s), component.mass);
        *constraints = (PhysicsConstraint){
            // TODO is element wise square correct? Seems insane
            .dir = energy_dir,
            .rot = energy_rot,
        };
        constraints += (vec3_mag_sq(energy_dir) <= f32_EPSILON && rotor3_mag_sq(energy_rot) <= f32_EPSILON);
    }

    scratch_dyn_end(scratch, constraints);

    // TODO Sort constraints or double sparse array?

    // TODO store the number of active components in state. then update it each frame. Use it to alloc an array BEFORE
    // constraints, then fill it. in that second loop. Other option is array of components_count size which is maximum
    // then shrink it post?
    RingBuff queue = ring_buff_create_from_scratch_space(scratch, sizeof(u32));
    // ring_buff_push_many(queue, components, components_count);
    // TODO do I construct a list in the components loop? Somewhere else? Or do i just write 1, 2, 3 etc now with a
    // loop? either way, skip dead/off/statics?

    // Physics Solve
    u32 current_idx = 0;
    while (!ring_buff_pop(&queue, &current_idx)) {
        PhysicsConstraint constraint = constraints_start[current_idx];
    }

    u32 constraints_count = (u32)(constraints - constraints_start);
    for (u32 idx = 0; idx < constraints_count; idx++) {
        // constraint; // TODO unimpl

        /*
          sparse graph seems good here?
          Need a way to say, get all constraints acting on component x
          is the solve just a graph traversal? ie solve x, x is constrained by a, b, c, so now deal with those? chain
          out? then how do I make sure the previous is handled? I don't right? It's just minimize energy progressively
          and have some fallback to ensure no looping? then does it matter if I handle a b c next or just at some point
          ie I run around in a queue and keep minimizing/adding to the queue as needed?

        */

        // queue_write(idx, queue_length);
        // queue_length += velocity != 0 && angular_velocity != 0; // this way we overwrite on the next insert
    }

    // Remaining Energy -> velocity -> position

    // TODO design decision, do I have holes or do I do a swapback array? Then I need a way to link
    // a component to a ubo etc and determine/handle death/removal.
    //
    // I think the answer is just do the math on the component anyways using bitwise masks
    for (u32 idx = 0; idx < state->components_count; idx++) {
        // TODO should probably just do this during the update
        vec3_add_ip(&state->components->position, vec3_mul_f32(state->components[idx].velocity, dt));
        // TODO pretty sure rotor3_mul_f32 is wrong here. that also affects scale, not just rotation
        rotor3_mul_ip(&state->components->rotation, rotor3_mul_f32(state->components[idx].angular_velocity, dt));
    }

    // TODO do I track which TRS/UBO were updated? or do I queue the writes into the store buffer during the loop?
    // I need a pointer or handle in each component then to trace them back, right?
}

PhysicsHandle physics_component_create(rwp(rw PhysicsState) state) {
    u32 slot = state->first_free_component_slot_idx;

    u32 filled = (slot == state->components_count);

    // Branchless:
    // if (slot == state->components_count) { state->components_count += 1; }
    PhysicsComponent* component = &state->components[slot];

    // TODO review this to make sure it's correct
    // u32 mask                             = -(u32)filled;
    // state->first_free_component_slot_idx = (~mask & component->next_empty) | (mask & state->components_count);
    state->first_free_component_slot_idx = branchless_or(filled, state->components_count, component->next_empty);
    state->components_count += filled;

    // memset is the technically superior version of: // reset struct to 0, but preserve generation
    // *component = (PhysicsComponent){
    //     .generation = component->generation,
    // };
    // assert(SCOPE_PHYSICS, offsetof(PhysicsComponent, generation) == 0);
    // memset((void*)component + offsetof(PhysicsComponent, generation) + sizeof_member(PhysicsComponent, generation),
    //        0,
    //        sizeof(PhysicsComponent) - sizeof_member(PhysicsComponent, generation));
    // NOTE the compiler (clang & gcc) correctly turns this into a partial memset
    u32 generation = component->generation;
    memset(component, 0, sizeof(PhysicsComponent));
    component->generation = generation;

    // NOTE NO NEED it's incremented on delete
    // component->generation += 1;

    return (PhysicsHandle){
        .idx        = slot,
        .generation = component->generation,
    };
}

void physics_component_delete(rwp(rw PhysicsState) state, ro PhysicsHandle handle) {
    PhysicsComponent* component = &state->components[handle.idx];

    // Branchless:
    // if (component->generation == handle.generation) {
    //     component->next_empty = state->first_free_component_slot_idx;
    //     component->generation += 1;
    //     state->first_free_component_slot_idx = handle.idx;
    // }
    u32 valid_handle = (component->generation == handle.generation);

    setif(valid_handle, component->next_empty, state->first_free_component_slot_idx);
    setif(valid_handle, state->first_free_component_slot_idx, handle.idx);

    component->generation += valid_handle;
}

// NOTE possibly returns NULL
// TODO should this just return a pointer to the 0th element which is a sentinel value per Anton/Wookash?
//      meh... hides errors though. Another possibility is a write func and this is pure read, no ptr return
//      How do I convey the get failed and your handle is bad? C style out param ptr? Gross.
rwp(rw PhysicsComponent) physics_component_get(rwp(rw PhysicsState) state, ro PhysicsHandle handle) {
    // PhysicsComponent physics_component_get(rwp(rw PhysicsState) state, ro PhysicsHandle handle) {
    PhysicsComponent* component = &state->components[handle.idx];
    // Branchless:
    // if (component->generation == handle.generation) { return component; } else { return NULL; }
    return (PhysicsComponent*)((u64)component * (component->generation == handle.generation));
    // return *component; // BUG what is the downside to just giving out this arbitrary data even if generation/slot is
    //                    // dead? It's just a read?
}

// void physics_component_write(rwp(rw PhysicsState) state, ro PhysicsHandle handle, ro PhysicsComponent component) {
//     if (handle.generation == state->components[handle.idx].generation) {
//         state->components[handle.idx]            = component;
//         state->components[handle.idx].generation = handle.generation;
//     }
//     // BUG this does not work for compound types
//     // Branchless:
//     // u32 handle_valid = (handle.generation == state->components[handle.idx].generation);
//     // setif(handle_valid, state->components[handle.idx], component); // this does not work... sadge
//     // setif(handle_valid, state->components[handle.idx].generation, handle.generation);
// }
