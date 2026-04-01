#include "App/BodySim.hpp"

#include <algorithm>
#include <cmath>

namespace TrueFlightApp
{

    using namespace NativeGame;

    float sampleGroundHeight(float x, float z, const TerrainFieldContext &terrainContext);
    Vec3 sampleTerrainNormal(float x, float y, float z, const TerrainFieldContext &terrainContext);

    namespace
    {

        inline int idx(BodyJointId id)
        {
            return static_cast<int>(id);
        }

        Vec3 safeNormalize(const Vec3 &v, const Vec3 &fallback = {0.0f, 1.0f, 0.0f})
        {
            const float lenSq = dot(v, v);
            if (lenSq <= 1.0e-8f)
            {
                return fallback;
            }
            return v / std::sqrt(lenSq);
        }

        float clamp01(float v)
        {
            return std::clamp(v, 0.0f, 1.0f);
        }

        float wrapPhase(float v)
        {
            constexpr float kTau = 6.2831853071795864769f;
            while (v >= kTau)
                v -= kTau;
            while (v < 0.0f)
                v += kTau;
            return v;
        }

        void setJoint(
            PhysicalBody &body,
            BodyJointId id,
            const Vec3 &pos,
            float mass,
            float radius)
        {
            BodySegment &j = body.joints[idx(id)];
            j.position = pos;
            j.previousPosition = pos;
            j.velocity = {};
            j.accumulatedForce = {};
            j.mass = mass;
            j.radius = radius;
            j.pinned = false;
        }

        void solveDistanceConstraint(BodySegment &a, BodySegment &b, float restLength, float stiffness)
        {
            const Vec3 delta = b.position - a.position;
            const float distSq = dot(delta, delta);
            if (distSq <= 1.0e-8f)
            {
                return;
            }

            const float dist = std::sqrt(distSq);
            const Vec3 n = delta / dist;
            const float error = dist - restLength;
            const float invMassA = a.pinned ? 0.0f : (a.mass > 0.0f ? 1.0f / a.mass : 0.0f);
            const float invMassB = b.pinned ? 0.0f : (b.mass > 0.0f ? 1.0f / b.mass : 0.0f);
            const float invMassSum = invMassA + invMassB;
            if (invMassSum <= 1.0e-8f)
            {
                return;
            }

            const Vec3 correction = n * (error * stiffness / invMassSum);
            if (!a.pinned)
            {
                a.position += correction * invMassA;
            }
            if (!b.pinned)
            {
                b.position -= correction * invMassB;
            }
        }

        Vec3 computeCenterOfMass(const PhysicalBody &body)
        {
            Vec3 com{};
            float totalMass = 0.0f;
            for (const BodySegment &j : body.joints)
            {
                com += j.position * j.mass;
                totalMass += j.mass;
            }
            if (totalMass <= 1.0e-8f)
            {
                return {};
            }
            return com / totalMass;
        }

        Vec3 bodyUp(const PhysicalBody &body)
        {
            const Vec3 up = body.joints[idx(BodyJointId::Chest)].position - body.joints[idx(BodyJointId::Pelvis)].position;
            return safeNormalize(up, {0.0f, 1.0f, 0.0f});
        }

        Vec3 bodyRight(const PhysicalBody &body)
        {
            const Vec3 r = body.joints[idx(BodyJointId::UpperArmR)].position - body.joints[idx(BodyJointId::UpperArmL)].position;
            return safeNormalize(Vec3{r.x, 0.0f, r.z}, {1.0f, 0.0f, 0.0f});
        }

        Vec3 bodyForward(const PhysicalBody &body)
        {
            const Vec3 up = bodyUp(body);
            const Vec3 right = bodyRight(body);
            return safeNormalize(cross(right, up), {0.0f, 0.0f, 1.0f});
        }

        float groundAt(const Vec3 &pos, const TerrainFieldContext &terrainContext)
        {
            return sampleGroundHeight(pos.x, pos.z, terrainContext);
        }

        bool jointGrounded(const BodySegment &joint, const TerrainFieldContext &terrainContext)
        {
            const float ground = groundAt(joint.position, terrainContext);
            return joint.position.y <= (ground + joint.radius + 0.035f);
        }

        void groundCollide(BodySegment &joint, const TerrainFieldContext &terrainContext, float friction)
        {
            const float ground = groundAt(joint.position, terrainContext);
            const float minY = ground + joint.radius;
            if (joint.position.y < minY)
            {
                joint.position.y = minY;
                if (joint.velocity.y < 0.0f)
                {
                    joint.velocity.y = 0.0f;
                }
                joint.velocity.x *= friction;
                joint.velocity.z *= friction;
            }
        }

        void applyPelvisHeightAndBalance(PhysicalBody &body, const TerrainFieldContext &terrainContext)
        {
            BodySegment &pelvis = body.joints[idx(BodyJointId::Pelvis)];
            BodySegment &footL = body.joints[idx(BodyJointId::FootL)];
            BodySegment &footR = body.joints[idx(BodyJointId::FootR)];

            body.balance.leftFootGrounded = jointGrounded(footL, terrainContext);
            body.balance.rightFootGrounded = jointGrounded(footR, terrainContext);
            body.balance.centerOfMass = computeCenterOfMass(body);

            const float groundL = groundAt(footL.position, terrainContext);
            const float groundR = groundAt(footR.position, terrainContext);

            if (body.balance.leftFootGrounded && body.balance.rightFootGrounded)
            {
                body.balance.supportCenter = (footL.position + footR.position) * 0.5f;
            }
            else if (body.balance.leftFootGrounded)
            {
                body.balance.supportCenter = footL.position;
            }
            else if (body.balance.rightFootGrounded)
            {
                body.balance.supportCenter = footR.position;
            }
            else
            {
                body.balance.supportCenter = pelvis.position;
            }

            const float supportGround =
                body.balance.leftFootGrounded && body.balance.rightFootGrounded ? 0.5f * (groundL + groundR) : body.balance.leftFootGrounded ? groundL
                                                                                                           : body.balance.rightFootGrounded  ? groundR
                                                                                                                                             : groundAt(pelvis.position, terrainContext);

            const float targetPelvisY = supportGround + body.tuning.pelvisHeight;
            const float pelvisYError = targetPelvisY - pelvis.position.y;

            pelvis.accumulatedForce.y +=
                (pelvisYError * body.tuning.pelvisHeightStrength) -
                (pelvis.velocity.y * body.tuning.pelvisHeightDamping);

            const Vec3 planarComError{
                (body.balance.supportCenter.x + body.balance.desiredComOffset.x) - body.balance.centerOfMass.x,
                0.0f,
                (body.balance.supportCenter.z + body.balance.desiredComOffset.z) - body.balance.centerOfMass.z};

            const Vec3 planarPelvisVel{pelvis.velocity.x, 0.0f, pelvis.velocity.z};

            pelvis.accumulatedForce +=
                (planarComError * (body.tuning.locomotionStrength * 0.75f)) -
                (planarPelvisVel * body.tuning.locomotionDamping);
        }

    } // namespace

    void initializeHumanoidBody(PhysicalBody &body, const Vec3 &rootPosition)
    {
        const Vec3 p = rootPosition;

        setJoint(body, BodyJointId::Pelvis, p + Vec3{0.0f, 1.00f, 0.0f}, 7.5f, 0.12f);
        setJoint(body, BodyJointId::Spine, p + Vec3{0.0f, 1.18f, 0.0f}, 4.0f, 0.10f);
        setJoint(body, BodyJointId::Chest, p + Vec3{0.0f, 1.38f, 0.02f}, 4.5f, 0.11f);
        setJoint(body, BodyJointId::Head, p + Vec3{0.0f, 1.64f, 0.05f}, 3.0f, 0.10f);

        setJoint(body, BodyJointId::UpperArmL, p + Vec3{-0.22f, 1.38f, 0.0f}, 1.5f, 0.07f);
        setJoint(body, BodyJointId::ForearmL, p + Vec3{-0.40f, 1.26f, 0.0f}, 1.1f, 0.06f);
        setJoint(body, BodyJointId::HandL, p + Vec3{-0.52f, 1.14f, 0.02f}, 0.6f, 0.05f);

        setJoint(body, BodyJointId::UpperArmR, p + Vec3{0.22f, 1.38f, 0.0f}, 1.5f, 0.07f);
        setJoint(body, BodyJointId::ForearmR, p + Vec3{0.40f, 1.26f, 0.0f}, 1.1f, 0.06f);
        setJoint(body, BodyJointId::HandR, p + Vec3{0.52f, 1.14f, 0.02f}, 0.6f, 0.05f);

        setJoint(body, BodyJointId::ThighL, p + Vec3{-0.11f, 0.78f, 0.0f}, 3.0f, 0.08f);
        setJoint(body, BodyJointId::ShinL, p + Vec3{-0.11f, 0.40f, 0.0f}, 2.2f, 0.07f);
        setJoint(body, BodyJointId::FootL, p + Vec3{-0.11f, 0.07f, 0.08f}, 1.1f, 0.06f);

        setJoint(body, BodyJointId::ThighR, p + Vec3{0.11f, 0.78f, 0.0f}, 3.0f, 0.08f);
        setJoint(body, BodyJointId::ShinR, p + Vec3{0.11f, 0.40f, 0.0f}, 2.2f, 0.07f);
        setJoint(body, BodyJointId::FootR, p + Vec3{0.11f, 0.07f, 0.08f}, 1.1f, 0.06f);

        body.constraints = {{
            {BodyJointId::Pelvis, BodyJointId::Spine, 0.18f, 1.0f},
            {BodyJointId::Spine, BodyJointId::Chest, 0.21f, 1.0f},
            {BodyJointId::Chest, BodyJointId::Head, 0.27f, 1.0f},

            {BodyJointId::Chest, BodyJointId::UpperArmL, 0.22f, 0.95f},
            {BodyJointId::UpperArmL, BodyJointId::ForearmL, 0.22f, 0.95f},
            {BodyJointId::ForearmL, BodyJointId::HandL, 0.17f, 0.95f},

            {BodyJointId::Chest, BodyJointId::UpperArmR, 0.22f, 0.95f},
            {BodyJointId::UpperArmR, BodyJointId::ForearmR, 0.22f, 0.95f},
            {BodyJointId::ForearmR, BodyJointId::HandR, 0.17f, 0.95f},

            {BodyJointId::Pelvis, BodyJointId::ThighL, 0.24f, 1.0f},
            {BodyJointId::ThighL, BodyJointId::ShinL, 0.38f, 1.0f},
            {BodyJointId::ShinL, BodyJointId::FootL, 0.33f, 1.0f},

            {BodyJointId::Pelvis, BodyJointId::ThighR, 0.24f, 1.0f},
            {BodyJointId::ThighR, BodyJointId::ShinR, 0.38f, 1.0f},
            {BodyJointId::ShinR, BodyJointId::FootR, 0.33f, 1.0f},
        }};

        body.desiredFacingForward = {0.0f, 0.0f, 1.0f};
        body.locomotionPhase = 0.0f;
        body.initialized = true;
    }

    void resetHumanoidBodyFromActor(PhysicalBody &body, const FlightState &actor)
    {
        const Vec3 rootPosition = actor.pos - Vec3{0.0f, body.tuning.pelvisHeight + body.tuning.actorPelvisOffset, 0.0f};
        initializeHumanoidBody(body, rootPosition);

        for (BodySegment &joint : body.joints)
        {
            joint.velocity = actor.vel;
            joint.previousPosition = joint.position - (actor.vel * (1.0f / 120.0f));
        }

        Vec3 actorForward = forwardFromRotation(actor.rot);
        actorForward.y = 0.0f;
        body.desiredFacingForward = safeNormalize(actorForward, {0.0f, 0.0f, 1.0f});
    }
    void applyPelvisLocomotionDrive(PhysicalBody &body, const BodyInput &input)
    {
        BodySegment &pelvis = body.joints[idx(BodyJointId::Pelvis)];

        const float moveMag = clamp01(std::sqrt((input.moveForward * input.moveForward) + (input.moveRight * input.moveRight)));
        const float targetSpeed =
            body.tuning.walkSpeed *
            (input.sprint ? body.tuning.sprintMultiplier : 1.0f) *
            moveMag;

        Vec3 facingForward = safeNormalize(
            Vec3{body.desiredFacingForward.x, 0.0f, body.desiredFacingForward.z},
            {0.0f, 0.0f, 1.0f});

        Vec3 facingRight = safeNormalize(cross(facingForward, Vec3{0.0f, 1.0f, 0.0f}), {1.0f, 0.0f, 0.0f});

        Vec3 desiredMoveDir{};
        if (moveMag > 1.0e-4f)
        {
            desiredMoveDir = safeNormalize(
                (facingForward * input.moveForward) + (facingRight * input.moveRight),
                facingForward);
            body.desiredFacingForward = desiredMoveDir;
        }
        else
        {
            desiredMoveDir = facingForward;
        }

        const Vec3 desiredPlanarVel = desiredMoveDir * targetSpeed;
        const Vec3 currentPlanarVel{pelvis.velocity.x, 0.0f, pelvis.velocity.z};

        pelvis.accumulatedForce +=
            (desiredPlanarVel - currentPlanarVel) *
            (body.tuning.locomotionStrength * pelvis.mass);

        if (input.jump && (body.balance.leftFootGrounded || body.balance.rightFootGrounded))
        {
            pelvis.velocity.y = std::max(pelvis.velocity.y, body.tuning.jumpVelocity);
            body.joints[idx(BodyJointId::Spine)].velocity.y = std::max(body.joints[idx(BodyJointId::Spine)].velocity.y, body.tuning.jumpVelocity * 0.75f);
            body.joints[idx(BodyJointId::Chest)].velocity.y = std::max(body.joints[idx(BodyJointId::Chest)].velocity.y, body.tuning.jumpVelocity * 0.55f);
        }
    }
    void driveBodyPoseTargets(PhysicalBody &body, const BodyInput &input, float dt)
    {
        const float moveMag = clamp01(std::sqrt((input.moveForward * input.moveForward) + (input.moveRight * input.moveRight)));

        body.balance.desiredComOffset = {
            input.moveRight * 0.05f,
            0.0f,
            input.moveForward * 0.10f};

        if (moveMag > 1.0e-4f)
        {
            Vec3 currentForward = safeNormalize(
                Vec3{body.desiredFacingForward.x, 0.0f, body.desiredFacingForward.z},
                {0.0f, 0.0f, 1.0f});
            Vec3 currentRight = safeNormalize(cross(currentForward, Vec3{0.0f, 1.0f, 0.0f}), {1.0f, 0.0f, 0.0f});

            const Vec3 desiredFacing = safeNormalize(
                (currentForward * input.moveForward) + (currentRight * input.moveRight),
                currentForward);

            body.desiredFacingForward = safeNormalize(
                lerp(body.desiredFacingForward, desiredFacing, std::clamp(dt * 10.0f, 0.0f, 1.0f)),
                desiredFacing);
        }

        applyPelvisLocomotionDrive(body, input);
    }

    void applyUprightAndFacing(PhysicalBody& body)
{
    BodySegment& pelvis = body.joints[idx(BodyJointId::Pelvis)];
    BodySegment& spine = body.joints[idx(BodyJointId::Spine)];
    BodySegment& chest = body.joints[idx(BodyJointId::Chest)];
    BodySegment& head = body.joints[idx(BodyJointId::Head)];
    BodySegment& upperArmL = body.joints[idx(BodyJointId::UpperArmL)];
    BodySegment& upperArmR = body.joints[idx(BodyJointId::UpperArmR)];

    const Vec3 currentUp = bodyUp(body);
    const Vec3 worldUp { 0.0f, 1.0f, 0.0f };
    const Vec3 uprightError = worldUp - currentUp;

    const Vec3 torsoCorrection = uprightError * body.tuning.uprightStrength;
    chest.accumulatedForce += torsoCorrection * chest.mass;
    head.accumulatedForce += torsoCorrection * (head.mass * 0.8f);
    spine.accumulatedForce += torsoCorrection * (spine.mass * 0.6f);
    pelvis.accumulatedForce -= torsoCorrection * (pelvis.mass * 0.4f);

    const Vec3 desiredForward = safeNormalize(
        Vec3{ body.desiredFacingForward.x, 0.0f, body.desiredFacingForward.z },
        { 0.0f, 0.0f, 1.0f });

    const Vec3 currentForward = safeNormalize(
        Vec3{ bodyForward(body).x, 0.0f, bodyForward(body).z },
        desiredForward);

    const Vec3 yawError = cross(currentForward, desiredForward);
    const Vec3 yawPlanar { yawError.x, 0.0f, yawError.z };

    chest.accumulatedForce += yawPlanar * (body.tuning.facingStrength * chest.mass);
    head.accumulatedForce += yawPlanar * (body.tuning.facingStrength * 0.7f * head.mass);
    upperArmR.accumulatedForce += yawPlanar * (body.tuning.facingStrength * 0.25f * upperArmR.mass);
    upperArmL.accumulatedForce += yawPlanar * (body.tuning.facingStrength * 0.25f * upperArmL.mass);
}


    void simulatePhysicalBody(
        PhysicalBody &body,
        float dt,
        const TerrainFieldContext &terrainContext)
    {
        if (!body.initialized)
        {
            return;
        }

        dt = std::clamp(dt, 0.0f, 0.033f);
        if (dt <= 0.0f)
        {
            return;
        }

        applyPelvisHeightAndBalance(body, terrainContext);
        applyUprightAndFacing(body);

        const BodySegment &pelvisRef = body.joints[idx(BodyJointId::Pelvis)];
        const Vec3 planarPelvisVel{pelvisRef.velocity.x, 0.0f, pelvisRef.velocity.z};
        const float moveMag = clamp01(length(planarPelvisVel) / std::max(0.001f, body.tuning.walkSpeed));

        body.locomotionPhase = wrapPhase(
            body.locomotionPhase +
            (dt * body.tuning.gaitFrequency * std::max(0.18f, moveMag)));

        const float phaseL = std::sin(body.locomotionPhase);
        const float phaseR = std::sin(body.locomotionPhase + 3.1415926535f);

        BodySegment &pelvis = body.joints[idx(BodyJointId::Pelvis)];
        BodySegment &footL = body.joints[idx(BodyJointId::FootL)];
        BodySegment &footR = body.joints[idx(BodyJointId::FootR)];

        const Vec3 desiredForward = safeNormalize(
            Vec3{body.desiredFacingForward.x, 0.0f, body.desiredFacingForward.z},
            {0.0f, 0.0f, 1.0f});
        const Vec3 desiredRight = safeNormalize(cross(desiredForward, Vec3{0.0f, 1.0f, 0.0f}), {1.0f, 0.0f, 0.0f});

        const float stride = body.tuning.strideLength * moveMag;
        const float liftL = std::max(0.0f, phaseL) * body.tuning.stepHeight;
        const float liftR = std::max(0.0f, phaseR) * body.tuning.stepHeight;

        Vec3 targetFootL =
            pelvis.position +
            (desiredRight * -body.tuning.stepWidth) +
            (desiredForward * (phaseL * stride)) +
            Vec3{0.0f, -body.tuning.pelvisHeight + footL.radius + liftL, 0.0f};

        Vec3 targetFootR =
            pelvis.position +
            (desiredRight * body.tuning.stepWidth) +
            (desiredForward * (phaseR * stride)) +
            Vec3{0.0f, -body.tuning.pelvisHeight + footR.radius + liftR, 0.0f};

        const float groundL = sampleGroundHeight(targetFootL.x, targetFootL.z, terrainContext);
        const float groundR = sampleGroundHeight(targetFootR.x, targetFootR.z, terrainContext);

        targetFootL.y = std::max(targetFootL.y, groundL + footL.radius + liftL);
        targetFootR.y = std::max(targetFootR.y, groundR + footR.radius + liftR);

        footL.accumulatedForce +=
            ((targetFootL - footL.position) * body.tuning.footPlantStrength) -
            (footL.velocity * body.tuning.footDamping);

        footR.accumulatedForce +=
            ((targetFootR - footR.position) * body.tuning.footPlantStrength) -
            (footR.velocity * body.tuning.footDamping);

        for (BodySegment &j : body.joints)
        {
            if (j.pinned)
            {
                j.accumulatedForce = {};
                j.velocity = {};
                j.previousPosition = j.position;
                continue;
            }

            const Vec3 accel = j.accumulatedForce / std::max(0.001f, j.mass);
            j.velocity += (accel + body.tuning.gravity) * dt;
            j.velocity *= body.tuning.damping;
            j.previousPosition = j.position;
            j.position += j.velocity * dt;
            j.accumulatedForce = {};
        }

        for (int iter = 0; iter < body.tuning.solverIterations; ++iter)
        {
            for (const BodyConstraint &c : body.constraints)
            {
                solveDistanceConstraint(
                    body.joints[idx(c.a)],
                    body.joints[idx(c.b)],
                    c.restLength,
                    c.stiffness);
            }

            for (BodySegment &j : body.joints)
            {
                groundCollide(j, terrainContext, body.tuning.groundFriction);
            }
        }

        for (BodySegment &j : body.joints)
        {
            j.velocity = (j.position - j.previousPosition) / std::max(0.0001f, dt);
        }
    }
    void syncActorFromPhysicalBody(FlightState& actor, const PhysicalBody& body)
{
    if (!body.initialized) {
        return;
    }

    const BodySegment& pelvis = body.joints[idx(BodyJointId::Pelvis)];
    const BodySegment& footL = body.joints[idx(BodyJointId::FootL)];
    const BodySegment& footR = body.joints[idx(BodyJointId::FootR)];

    actor.pos = pelvis.position + Vec3{ 0.0f, body.tuning.actorPelvisOffset, 0.0f };
    actor.vel = pelvis.velocity;
    actor.flightVel = pelvis.velocity;
    actor.flightAngVel = {};

    const float pelvisToFoot = pelvis.position.y - std::min(footL.position.y, footR.position.y);
    actor.onGround = pelvisToFoot <= (body.tuning.pelvisHeight + 0.12f);

    Vec3 forward = safeNormalize(
        Vec3{ body.desiredFacingForward.x, 0.0f, body.desiredFacingForward.z },
        { 0.0f, 0.0f, 1.0f });
    const Vec3 up { 0.0f, 1.0f, 0.0f };
    Vec3 right = safeNormalize(cross(up, forward), { 1.0f, 0.0f, 0.0f });
    forward = safeNormalize(cross(right, up), { 0.0f, 0.0f, 1.0f });

    const Quat targetRot = quatFromBasisOrthonormal(right, up, forward);
    actor.rot = quatNormalizeSafe(nlerp(actor.rot, targetRot, 0.22f));
}
} // namespace TrueFlightApp