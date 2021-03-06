#ifndef COLLABORATION_H
#define COLLABORATION_H

#include <string>
#include <deque>
#include <map>
#include <ctime>

#include <yarp/os/all.h>
#include <yarp/sig/all.h>
#include <yarp/math/Math.h>
#include <yarp/math/Rand.h>
#include <iCub/ctrl/filters.h>

#include <icubclient/all.h>
#include "kinectWrapper/kinectWrapper.h"

#include <opencv2/opencv.hpp>

#include "collaboration_IDL.h"

#define MODE_RECEIVE    1
#define MODE_GIVE       2
#define MODE_IDLE       0

#define OPENHAND        0
#define CLOSEHAND       1

using namespace std;
using namespace yarp::os;
using namespace yarp::sig;
using namespace yarp::math;
using namespace iCub::ctrl;
using namespace icubclient;

class collaboration : public RFModule, public collaboration_IDL
{
protected:
    double      period;
    string      name;
    string      robot;
    string      part;
    string      _arm;
    double      posTol;
    double      moveDuration;                       //!< moving time for reactCtrl

    bool        connectedReactCtrl, connectedARE, connectedSQR;
    int8_t      running_mode;

    RpcServer   rpcPort;                            //!< rpc server to receive user request
    OPCClient   *opc;                               //!< OPC client object
    RpcClient   rpcReactCtrl;                       //!< rpc client port to send requests to /reactController/rpc
    RpcClient   rpcARE;                             //!< rpc client port to send requests to /actionsRenderingEngine/cmd:io
    RpcClient   rpcGraspSQR;                        //!< rpc client port to send requests to /graspProcessor/rpc
    RpcClient   rpcSkeleton3D;
    RpcClient   rpcIOLSMH;
    string      partner_default_name;

    Vector      homePosL, homePosR, basket, graspTorso, graspGaze;
    double      workspaceX, workspaceY, workspaceZ_low, workspaceZ_high;

    bool                    isHoldingObject;                    //!< bool variable to show if object is holding, assume that a sucessful grasp action means robot holding the object
    icubclient::Object      *manipulatingObj;


    // Interfaces for the torso
    yarp::dev::PolyDriver           ddT;
    yarp::dev::PolyDriver           ddG; //!< gaze  controller  driver
    yarp::dev::PolyDriver           ddA; //!< arm  controller  driver
    yarp::dev::PolyDriver           ddA_joint;
    yarp::dev::IEncoders            *iencsT;
    yarp::dev::IVelocityControl     *ivelT;
    yarp::dev::IPositionControl     *iposT;
    yarp::dev::IControlMode         *imodT;
    yarp::dev::IControlLimits       *ilimT;
    yarp::sig::Vector               *encsT;
    int jntsT;

    // Arm interface
    yarp::dev::ICartesianControl    *icartA;
    int                             contextArm;
    int                             contextReactCtrl;

    // Arm joint controller for grasping
    yarp::dev::IControlMode         *imodA;
    yarp::dev::IPositionControl     *iposA;
    yarp::dev::IEncoders            *iencsA;
    yarp::dev::IControlLimits       *ilimA;
    yarp::sig::Vector               *encsA;
    Vector                          closedHandPos, openHandPos, midHandPos;
    Vector                          handVels;
    int jntsA;

    // Gaze interface
    yarp::dev::IGazeControl         *igaze;
    yarp::dev::IPositionControl     *iposG;
    int                             contextGaze;
    Vector                          homeAng;

    bool    configure(ResourceFinder &rf);
    bool    interruptModule();
    bool    close();
    bool    attach(RpcServer &source);
    double  getPeriod();
    bool    updateModule();

    bool    moveReactPPS(const string &target, const string &arm, const double &timeout=10.,
                         const bool &isTargetHuman = false);

    bool    moveReactThenGrasp(const string &target, const string &arm, const double &timeout=10.);

    bool    moveReactThenGive(const string &target, const string &arm, const double &timeout=10.);

    bool    moveReactPPS(const Vector &pos, const string &arm, const double &timeout=10.);

    bool    homeARE();

    bool    stopReactPPS();

    bool    takeARE(const string &target, const string &arm);

    bool    takeARE(const Vector &pos, const string &arm);

    bool    graspARE(const Vector &pos, const string &arm);

    bool    closeHand(const string &arm, const double &timeout=10.0);

    bool    openHand(const string &arm, const double &timeout=10.0);

    bool    moveFingers(const int &action, const string &arm, const double &timeout=10.0);

    bool    moveFingersToWp(const Vector &Wp, const double &timeout=10.0);

    bool    reachArm(const Vector &pos, const string &arm, const double &timeout=3.0);

    bool    graspRaw(const Vector &pos, const string &arm);

    bool    getGraspConfig(const Bottle &b, Vector &openPos, Vector &midPos,
                           Vector &closedPos, Vector &vels);

    bool    graspOnTable(const string &target, const string &arm);

    /**
     * @brief move move a robot arm with simple cartesian controller
     * @param pos Vector of 3D Cartesian position
     * @return True/False if completing action sucessfully or not
     */
    bool    move(const Vector &pos, const string &arm);

    bool    giveARE(const string &target, const string &arm);

    bool    giveARE(const Vector &pos, const string &arm);

    bool    dropARE(const Vector &pos, const string &arm);

    bool    checkPosReachable(const Vector &pos, const string &arm);

    bool    lookAtHome(const Vector &ang, const double &timeout);

    /**
     * @brief updateHoldingObj update the position of the object hold by robot hand
     * @param x_EE End-effector position
     * @param o_EE End-effector orientation in axis-angle format
     * @return True/False if completing action sucessfully or not
     */
    bool    updateHoldingObj(const Vector &x_EE, const Vector &o_EE);

    bool    setHumanHandValence(const double &valence,const string &_human_part);

    bool    setHumanValence(const double &valence);

    bool    getHumanValence(double &valence);

public:

    /**
     * @brief receive_object whole procedure to receive an object from human
     * @param _object
     * @return
     */   
    bool    receive_object(const string &_object, const double _wait=-1.0)
    {
        if (_wait>=0.0)
            Time::delay(_wait);
        running_mode = MODE_RECEIVE;
        string arm = _arm;
        Vector homePos(3,0.0);
        if (arm=="left")
            homePos = homePosL;
        else if (arm=="right")
            homePos = homePosR;
        else
            return false;

//        bool ok = moveReactPPS(_object, arm);
//        isHoldingObject = takeARE(_object, arm);
//        ok = ok && isHoldingObject;
        isHoldingObject = moveReactThenGrasp(_object,arm,moveDuration);
        bool ok = isHoldingObject;

        Time::delay(0.5);
        lookAtHome(homeAng,5.0);

//        ok = ok && moveReactPPS(homePos, arm,moveDuration);
//        lookAtHome(homeAng,5.0);
        ok = ok && home_ARE();

        double humanValence=0.0;
        if (getHumanValence(humanValence))
            setHumanValence(-1.0);

        ok = ok && pre_grasp_pos();
        Time::delay(1.0);
        ok = ok && dropARE(basket, arm);
        if (ok)
        {
            manipulatingObj->m_ego_position = basket;
            manipulatingObj->m_value = 0.0;
            opc->commit(manipulatingObj);
            isHoldingObject = false;
        }


        Time::delay(0.5);
        Vector homeTorso(3,0.0);
        ok = ok && move_torso(homeTorso);

        Time::delay(2.);
        ok = ok && home_ARE();

        lookAtHome(homeAng,5.0);
        setHumanValence(humanValence);
        running_mode = MODE_IDLE;
        return ok;
    }

    bool    move_pos_React(const Vector &_pos, const double _timeout)
    {
        return moveReactPPS(_pos,_arm, _timeout);
    }

    /**
     * @brief hand_over_object whole procedure to give an object to human
     * @param _object
     * @return
     */
    bool    hand_over_object(const string &_object, const string &_human_part)
    {
        running_mode = MODE_GIVE;
        string arm = _arm;
        Vector homePos(3,0.0);
        if (arm=="left")
            homePos = homePosL;
        else if (arm=="right")
            homePos = homePosR;
        else
            return false;

        // TODO grasp on table
//        bool ok = pre_grasp_pos();    // deactivate for the expriments
//        Time::delay(5.0);

        double humanValence=0.0;
        if (getHumanValence(humanValence))
            setHumanValence(-1.0);

        bool ok = true;
        isHoldingObject = graspOnTable(_object, arm);

        if (isHoldingObject)
        {
            Object *obj=opc->addOrRetrieveEntity<Object>(_object);
            obj->m_value=-1.0;
            obj->m_present=1.0;
            manipulatingObj = obj;
            Vector x_cur(3,0.0), o_cur(4,0.0);
            if (icartA->getPose(x_cur, o_cur))
                updateHoldingObj(x_cur, o_cur);
        }

        Time::delay(0.5);
        lookAtHome(homeAng,5.0);
        // TODO: reduce the valence of _human_part to receive object
        setHumanValence(humanValence);
        setHumanHandValence(-1.0,_human_part);

        ok = ok && isHoldingObject;
//        ok = ok && moveReactPPS(_human_part, arm, 10.0, true);   //move to near empty hand
//        ok = ok && giveARE(_human_part, arm);       //give to empty hand

        ok = ok && moveReactThenGive(_human_part,arm, moveDuration);

        if (ok)
            isHoldingObject = false;

        Time::delay(0.5);
        lookAtHome(homeAng,5.0);
        setHumanHandValence(0.0,_human_part);

//        ok = ok && moveReactPPS(homePos, arm, moveDuration);
//        Time::delay(0.5);
        home_all();

        running_mode = MODE_IDLE;
        return ok;
    }

    bool    take_pos_ARE(const Vector &_pos, const string &_arm)
    {
        return takeARE(_pos,_arm);
    }

    bool    drop_pos_ARE(const Vector &_pos, const string &_arm)
    {
        return dropARE(_pos,_arm);
    }

    bool    grasp_pos_ARE(const Vector &_pos, const string &_arm)
    {
        return graspARE(_pos, _arm);
    }

    bool    grasp_pos_Raw(const Vector &_pos, const string &_arm)
    {
        return graspRaw(_pos, _arm);
    }

    bool    grasp_on_table(const string &_target, const string &_arm)
    {
        return graspOnTable(_target,_arm);
    }

    bool    give_human_ARE(const string &_partH, const string &_armR)
    {
        return giveARE(_partH, _armR);
    }

    bool    stop_React()
    {
        return stopReactPPS();
    }

    bool    home_ARE()
    {
        return homeARE();
    }

    bool    home_all()
    {
        if (homeARE())
            return lookAtHome(homeAng, 5.0);
        else
            return false;
    }

    bool    pre_grasp_pos()
    {
        bool ok= move_torso(graspTorso);
        Time::delay(0.5);
        return ok && lookAtHome(graspGaze, 5.0);
    }

    bool    move_torso(const Vector &_ang)
    {
        Vector ang = _ang;
        for (int8_t i=0; i<3; i++)
            imodT->setControlMode(i, VOCAB_CM_POSITION);
        return iposT->positionMove(ang.data());
    }

    bool    move_neck(const Vector &_ang)
    {
        return lookAtHome(_ang,5.0);
    }

    bool    set_posTol(const double _tol)
    {
        if (_tol>0.0)
        {
            posTol = _tol;
            return true;
        }
        else
            return false;

    }

    double    get_posTol()
    {
        return posTol;
    }

    bool    set_moveDuration(const double _time)
    {
        if (_time>0.0)
        {
            moveDuration = _time;
            return true;
        }
        else
            return false;

    }

    double    get_moveDuration()
    {
        return moveDuration;
    }

    bool    set_homeAng(const Vector &_angs)
    {
        if (_angs.size()==3)
        {
            homeAng = _angs;
            return true;
        }
        else
            return false;
    }

    bool    close_hand(const string &_arm)
    {
        return closeHand(_arm);
    }

    bool    open_hand(const string &_arm)
    {
        return openHand(_arm);
    }

    bool    reduce_human_valence(const string &_human_part)
    {
        return setHumanHandValence(-1.0,_human_part);
    }
};

#endif // COLLABORATION_H
