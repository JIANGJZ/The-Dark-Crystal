#include "Character.h"
#include "Agent.h"
#include "ConfigurationManager.h"
#include "ClosestNotMeNotDynamicObjectConvexResultCallback.h"

#include <Scene/Scene.hpp>

#include <cstdio>

const QString Character::WALK_SOUND_COMPONENT = "walk_sound";
const QString Character::JUMP_SOUND_COMPONENT = "jump_sound";
const QString Character::RUN_SOUND_COMPONENT = "run_sound";

Character::Character(const QString node_name, const QString mesh_handle, const dt::PhysicsBodyComponent::CollisionShapeType collision_shape_type, 
    const btScalar mass, const QString walk_sound_handle, const QString jump_sound_handle, const QString run_sound_handle, const float jump_speed)
	: Entity(node_name, mesh_handle, collision_shape_type, mass),
	mWalkSoundHandle(walk_sound_handle),
	mJumpSoundHandle(jump_sound_handle),
	mRunSoundHandle(run_sound_handle),
	mVelocity(0.0, 0.0, 0.0),
	mJumpSpeed(jump_speed),
	mTimeElapseAfterJumping(1e6) {
		Entity::mIsJumping = false; 
}

void Character::onInitialize() {
    Entity::onInitialize();

    auto conf_mgr = ConfigurationManager::getInstance() ;
    SoundSetting sound_setting = conf_mgr->getSoundSetting();

    auto walk_sound = this->addComponent<dt::SoundComponent>(new dt::SoundComponent(mWalkSoundHandle, WALK_SOUND_COMPONENT));
    auto jump_sound = this->addComponent<dt::SoundComponent>(new dt::SoundComponent(mJumpSoundHandle, JUMP_SOUND_COMPONENT));
    auto run_sound = this->addComponent<dt::SoundComponent>(new dt::SoundComponent(mRunSoundHandle, RUN_SOUND_COMPONENT));

    walk_sound->setVolume((float)sound_setting.getSoundEffect());
    jump_sound->setVolume((float)sound_setting.getSoundEffect());
    run_sound->setVolume((float)sound_setting.getSoundEffect());

    walk_sound->getSound().setLoop(true);
    run_sound->getSound().setLoop(true);

    auto physics_body = this->findComponent<dt::PhysicsBodyComponent>(PHYSICS_BODY_COMPONENT);

    // 化身Kinematic Body拯救世界！！！
    physics_body->getRigidBody()->setCollisionFlags(physics_body->getRigidBody()->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
    physics_body->getRigidBody()->setActivationState(DISABLE_DEACTIVATION);

	// 初始化跳跃冷却时间
	mJumpingInterval = mJumpSpeed / physics_body->getRigidBody()->getGravity().length();
}

void Character::onDeinitialize() {
    Entity::onDeinitialize();
}

void Character::onUpdate(double time_diff) {
    this->mIsUpdatingAfterChange = (time_diff == 0);

	// 处理跳跃后的时间
	if (mIsJumping) {
		mTimeElapseAfterJumping += time_diff;
		if (mTimeElapseAfterJumping > mJumpingInterval + 1e-5) {
			mTimeElapseAfterJumping = mJumpingInterval + 1e-5;
		}
	}

    auto physics_body = this->findComponent<dt::PhysicsBodyComponent>(PHYSICS_BODY_COMPONENT);
    btMotionState* motion = physics_body->getRigidBody()->getMotionState();
    auto gravity = physics_body->getRigidBody()->getGravity();  // 这Gravity是重力加速度…… >_<
    
    Ogre::Vector3 normalized_move_vector = mMoveVector;
    normalized_move_vector.normalise();

    auto new_velocity = BtOgre::Convert::toBullet(getRotation(dt::Node::SCENE) * normalized_move_vector * mCurSpeed);

    btTransform target_position;
    motion->getWorldTransform(target_position);
    
    btTransform possible_position;
    possible_position.setOrigin(BtOgre::Convert::toBullet(getPosition(dt::Node::SCENE)));
    possible_position.setRotation(target_position.getRotation());

    if (!mIsJumping) {
        mVelocity.setX(new_velocity.x());
        mVelocity.setZ(new_velocity.z());
    }

    if (__canJump() && this->isOnGround()) {
        if (mVelocity.getY() < 0.0f) {
            // 人物已在地面上，因此将掉落速度清零。
            mVelocity.setY(0.0f);
        }

        auto mesh = this->findComponent<dt::MeshComponent>(MESH_COMPONENT);

		// 刚刚落地
		if (mIsJumping && mesh->isAnimationStopped()) {
			mIsJumping = false;

			mesh->stopAnimation();

			if (!mIsMoving) {
				mVelocity.setZero();
				mMoveVector = Ogre::Vector3::ZERO;
			}

			if (!mVelocity.isZero()) {
                if (mHasSpeededUp) {
                    mesh->setAnimation("run");
                } else {
                    mesh->setAnimation("walk");
                }

                mesh->setLoopAnimation(true);
                mesh->playAnimation();
            }
        }

    } else {
        mVelocity += gravity * time_diff;
    }

    target_position.setOrigin(target_position.getOrigin() + mVelocity * time_diff);

    if (__canMoveTo(target_position, possible_position)) {
        // 竟然能够移动到这里！！！
        motion->setWorldTransform(target_position);

    } else {
        // 移动不到……

        btVector3 vec = BtOgre::Convert::toBullet(this->getPosition(dt::Node::SCENE)) - target_position.getOrigin();

        if (!vec.isZero())
            vec.normalize();

        mVelocity.setX(vec.x());
        if (mIsJumping) {
            mVelocity.setY(vec.y());
        }
        mVelocity.setZ(vec.z());
    }

    //this->mIsUpdatingAfterChange = false;

    Node::onUpdate(time_diff);
}

void Character::setJumpSpeed(const float jump_speed) {
    mJumpSpeed = jump_speed;
}

float Character::getJumpSpeed() const {
    return mJumpSpeed;
}

void Character::__onMove(Entity::MoveType type, bool is_pressed) {
    if (!mIsJumping) {
        bool is_stopped = false;

        switch (type) {
        case FORWARD:
            if (is_pressed && mMoveVector.z > -1.0f)
                mMoveVector.z -= 1.0f; // Ogre Z轴正方向为垂直屏幕向外。
            else if (!is_pressed && mMoveVector.z < 1.0f)
                mMoveVector.z += 1.0f;

            break;

        case BACKWARD:
            if (is_pressed && mMoveVector.z < 1.0f)
                mMoveVector.z += 1.0f;
            else if (!is_pressed && mMoveVector.z > -1.0f)
                mMoveVector.z -= 1.0f;

            break;

        case LEFTWARD:
            if (is_pressed && mMoveVector.x > -1.0f)
                mMoveVector.x -= 1.0f;
            else if (!is_pressed && mMoveVector.x < 1.0f)
                mMoveVector.x += 1.0f;

            break;

        case RIGHTWARD:
            if (is_pressed && mMoveVector.x < 1.0f)
                mMoveVector.x += 1.0f;
            else if (!is_pressed && mMoveVector.x > -1.0f)
                mMoveVector.x -= 1.0f;

            break;

        case STOP:
            mMoveVector.x = 0.0f;
            mMoveVector.z = 0.0f;
            is_stopped = true;

            break;

        default:
            dt::Logger::get().debug("Not processed MoveType!");
        }

        auto mesh = this->findComponent<dt::MeshComponent>(MESH_COMPONENT);

        mesh->stopAnimation();

        if (is_stopped) {
            this->findComponent<dt::SoundComponent>(WALK_SOUND_COMPONENT)->stopSound();
            this->findComponent<dt::SoundComponent>(RUN_SOUND_COMPONENT)->stopSound();
        } else {
            std::shared_ptr<dt::SoundComponent> move_sound;

            if (mHasSpeededUp) {
                move_sound = this->findComponent<dt::SoundComponent>(RUN_SOUND_COMPONENT);
                mesh->setAnimation("run");
            } else {
                move_sound = this->findComponent<dt::SoundComponent>(WALK_SOUND_COMPONENT);
                mesh->setAnimation("walk");
            }

            mesh->setLoopAnimation(true);
            mesh->playAnimation();
            move_sound->playSound();
        }

        mIsMoving = !is_stopped;
    } else {
        mIsMoving = (type != STOP);
    }
}

void Character::__onJump(bool is_pressed) {
    auto physics_body = this->findComponent<dt::PhysicsBodyComponent>(PHYSICS_BODY_COMPONENT);

    if (is_pressed && __canJump() && isOnGround()) {
        mVelocity.setY(mJumpSpeed);		
		mTimeElapseAfterJumping = 0.0f;

        this->findComponent<dt::SoundComponent>(JUMP_SOUND_COMPONENT)->playSound();

        auto mesh = this->findComponent<dt::MeshComponent>(MESH_COMPONENT);

        mesh->stopAnimation();
        mesh->setAnimation("jump-begin");
        mesh->setLoopAnimation(false);
        mesh->playAnimation();

        mIsJumping = true;
    }
}

void Character::__onSpeedUp(bool is_pressed) {
    float increasing_rate = 1.5f;

    if (is_pressed /* && !mIsJumping */) {
		if (mCurSpeed == mOrigSpeed) {
			this->setCurSpeed(mOrigSpeed * increasing_rate);
		}

        if (!mIsJumping && mIsMoving) {
            this->findComponent<dt::SoundComponent>(WALK_SOUND_COMPONENT)->stopSound();
            this->findComponent<dt::SoundComponent>(RUN_SOUND_COMPONENT)->playSound();

            auto mesh = this->findComponent<dt::MeshComponent>(MESH_COMPONENT);
            mesh->setAnimation("walk");
            mesh->stopAnimation();
            mesh->setAnimation("run");
            mesh->setLoopAnimation(true);
            mesh->playAnimation();
        }
    } else {
		if (mCurSpeed != mOrigSpeed) {
			this->setCurSpeed(mOrigSpeed);
		}

		if (!mIsJumping && mIsMoving) {
            this->findComponent<dt::SoundComponent>(RUN_SOUND_COMPONENT)->stopSound();
            this->findComponent<dt::SoundComponent>(WALK_SOUND_COMPONENT)->playSound();

            auto mesh = this->findComponent<dt::MeshComponent>(MESH_COMPONENT);
            mesh->setAnimation("run");
            mesh->stopAnimation();
            mesh->setAnimation("walk");
            mesh->setLoopAnimation(true);
            mesh->playAnimation();
        }
    }

    mHasSpeededUp = is_pressed;
}

void Character::__onLookAround(Ogre::Quaternion body_rot, Ogre::Quaternion agent_rot) {
	//先旋转镜头和武器
	auto agent = this->findChildNode(Agent::AGENT);
	Ogre::Radian pitch = agent->getRotation().getPitch() + agent_rot.getPitch();

	if (pitch > Ogre::Degree(89.9)) {
		pitch = Ogre::Degree(89.9);
	}
	if (pitch < Ogre::Degree(-89.9)) {
		pitch = Ogre::Degree(-89.9);
	}
	auto pitch_rot = Ogre::Quaternion(pitch, Ogre::Vector3(1, 0, 0));
	agent->setRotation(pitch_rot);

	Ogre::Quaternion rotation = Ogre::Quaternion((this->getRotation() * body_rot).getYaw(), Ogre::Vector3(0, 1, 0));

	auto physics_body = this->findComponent<dt::PhysicsBodyComponent>(PHYSICS_BODY_COMPONENT);
	auto motion = physics_body->getRigidBody()->getMotionState();
	btTransform trans;

	motion->getWorldTransform(trans);
	trans.setRotation(BtOgre::Convert::toBullet(rotation));
	motion->setWorldTransform(trans);
}

bool Character::__canMoveTo(const btTransform& position, btTransform& closest_position) {
    auto physics_body = this->findComponent<dt::PhysicsBodyComponent>(PHYSICS_BODY_COMPONENT);
    ClosestNotMeNotDynamicObjectConvexResultCallback callback(physics_body->getRigidBody());
    
    btTransform target = position;
    btVector3 origin = target.getOrigin();
    origin.setY(origin.y() + 0.01f);
    target.setOrigin(origin);

    this->getScene()->getPhysicsWorld()->getBulletWorld()->convexSweepTest(dynamic_cast<btConvexShape*>(physics_body->getRigidBody()->getCollisionShape()), 
        physics_body->getRigidBody()->getWorldTransform(), target, callback);

    btRigidBody* rigid_body = dynamic_cast<btRigidBody*>(callback.m_hitCollisionObject);

    if (callback.hasHit() && rigid_body != nullptr) {

        return false;
    }
    
    return true;
}

bool Character::__canJump() {
	return mTimeElapseAfterJumping + 1e-5 > mJumpingInterval;
}