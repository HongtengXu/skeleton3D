/* Copyright (C) 2017 RobotCub Consortium
 * Author:  Phuong Nguyen
 * email:   phuong.nguyen@iit.it
 * website: www.robotcub.org
 * Permission is granted to copy, distribute, and/or modify this program
 * under the terms of the GNU General Public License, version 2 or any
 * later version published by the Free Software Foundation.
 *
 * A copy of the license can be found at
 * http://www.robotcub.org/"+robot+"/license/gpl.txt
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details
*/

#include <stdio.h>
#include <string>
#include <cctype>
#include <algorithm>
#include <map>

#include <yarp/os/all.h>
#include <yarp/dev/all.h>
#include <yarp/sig/all.h>
#include <yarp/math/Math.h>

#include <iCub/ctrl/math.h>
#include <iCub/skinDynLib/common.h>

#include <gsl/gsl_math.h>

#include <iCub/iKin/iKinFwd.h>

#include <iostream>
#include <string>
#include <stdio.h>
#include <stdarg.h>
#include <vector>
#include <set>
#include <list>

#include <iCub/ctrl/math.h>
#include <iCub/periPersonalSpace/skinPartPWE.h>
#include <iCub/skinDynLib/skinContact.h>
#include <iCub/skinDynLib/skinContactList.h>

#define SKIN_THRES	        7 // Threshold with which a contact is detected

using namespace std;
using namespace yarp;
using namespace yarp::os;
using namespace yarp::sig;
using namespace yarp::math;
using namespace yarp::dev;
using namespace iCub::iKin;
using namespace iCub::ctrl;
using namespace iCub::skinDynLib;

class visuoTactileCalib: public RFModule
{
protected:
    string          name;
    string          robot;
    double          arm_version;
    double          period;
    int             verbosity;
    double          timeNow;

    vector<Vector>  contactPts;     //!< vector of skin contact points at a moment


    // Driver for "classical" interfaces
    PolyDriver          ddR;        //!< right arm device driver
    PolyDriver          ddL;        //!< left arm  device driver
    PolyDriver          ddT;        //!< torso controller  driver
    PolyDriver          ddH;        //!< head  controller  driver
    PolyDriver          ddG;        //!< gaze  controller  driver

    // "Classical" interfaces - RIGHT ARM
    IEncoders           *iencsR;
    yarp::sig::Vector   *encsR;
    iCubArm             *armR;
    int                 jntsR;  //all joints including fingers ~ 16
    int                 jntsAR; //arm joints only ~ 7
    // "Classical" interfaces - LEFT ARM
    IEncoders           *iencsL;
    yarp::sig::Vector   *encsL;
    iCubArm             *armL;
    int                 jntsL;  //all joints including fingers ~ 16
    int                 jntsAL; //arm joints only ~ 7
    // "Classical" interfaces - TORSO
    IEncoders           *iencsT;
    yarp::sig::Vector   *encsT;
    int                 jntsT;
    // "Classical" interfaces - HEAD
    IEncoders           *iencsH;
    yarp::sig::Vector   *encsH;
    int                 jntsH;
    // Gaze controller interface
    IGazeControl        *igaze;
    int contextGaze;

    ResourceFinder    armsRF;

    //N.B. All angles in this thread are in degrees
    yarp::sig::Vector qL;           //!< current values of left arm joints (should be 7)
    yarp::sig::Vector qR;           //!< current values of right arm joints (should be 7)
    yarp::sig::Vector qT;           //!< current values of torso joints (3, in the order expected for iKin: yaw, roll, pitch)

    // Stamp for the setEnvelope for the ports
    yarp::os::Stamp ts;

    vector<string>          filenames;
    vector <skinPartPWE>    iCubSkin;
    int                     iCubSkinSize;

    string                  modality;                                       //!< modality to use (either 1D or 2D)

    BufferedPort<iCub::skinDynLib::skinContactList> skinPortIn;             //!< input from the skinManager
    Port                                            contactDumperPortOut;   //!< output to dump the contact points in World FoR

    //********************************************
    // From vtRFThread
    bool detectContact(iCub::skinDynLib::skinContactList *_sCL, int &idx,
                                   std::vector <unsigned int> &idv)
    {
        // Search for a suitable contact. It has this requirements:
        //   1. it has to be higher than SKIN_THRES
        //   2. more than two taxels should be active for that contact (in order to avoid spikes)
        //   3. it should be in the proper skinpart (forearms and hands)
        //   4. it should activate one of the taxels used by the module
        //      (e.g. the fingers will not be considered)
        for(iCub::skinDynLib::skinContactList::iterator it=_sCL->begin(); it!=_sCL->end(); it++)
        {
            idv.clear();
            if( it -> getPressure() > SKIN_THRES && (it -> getTaxelList()).size() > 2 )
            {
                for (int i = 0; i < iCubSkinSize; i++)
                {
                    if (SkinPart_s[it -> getSkinPart()] == iCubSkin[i].name)
                    {
                        idx = i;
                        std::vector <unsigned int> txlList = it -> getTaxelList();

                        bool itHasBeenTouched = false;

                        getRepresentativeTaxels(txlList, idx, idv);

                        if (idv.size()>0)
                        {
                            itHasBeenTouched = true;
                        }

                        if (itHasBeenTouched)
                        {
                            if (verbosity>=1)
                            {
                                printMessage(1,"Contact! Skin part: %s\tTaxels' ID: ",iCubSkin[i].name.c_str());
                                for (size_t i = 0; i < idv.size(); i++)
                                    printf("\t%i",idv[i]);

                                printf("\n");
                            }
                            yInfo("Contact! Skin part: %s\t Taxels' ID: ",iCubSkin[i].name.c_str());
                            for (size_t j=0; j<idv.size(); j++)
                            {
                                yDebug("iCubSkin[%i].taxels.size() = %lu",i,iCubSkin[i].taxels.size());
                                for (size_t k=0; k<iCubSkin[i].taxels.size(); k++)
                                    if (iCubSkin[i].taxels[k]->getID() == idv[j])
                                    {
                                        Vector contact(3,0.0);
                                        contact = iCubSkin[i].taxels[k]->getWRFPosition();
                                        yInfo("\t%i, WRFPos %s ", idv[j], contact.toString().c_str());
                                        contactPts.push_back(contact);
                                    }
                            }

                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    //see also Compensator::setTaxelPosesFromFile in icub-main/src/modules/skinManager/src/compensator.cpp
    // From vtRFThread
    /**
    * Finds out the positions of the taxels w.r.t. their respective limbs
    **/
    bool setTaxelPosesFromFile(const string filePath, skinPartPWE &sP)
    {
        string line;
        ifstream posFile;
        yarp::sig::Vector taxelPos(3,0.0);
        yarp::sig::Vector taxelNrm(3,0.0);
        yarp::sig::Vector taxelPosNrm(6,0.0);

        string filename = strrchr(filePath.c_str(), '/');
        filename = filename.c_str() ? filename.c_str() + 1 : filePath.c_str();

        // Assign the name and version of the skinPart according to the filename (hardcoded)
        if      (filename == "left_forearm_mesh.txt")    { sP.setName(SkinPart_s[SKIN_LEFT_FOREARM]);  sP.setVersion("V1");   }
        else if (filename == "left_forearm_nomesh.txt")  { sP.setName(SkinPart_s[SKIN_LEFT_FOREARM]);  sP.setVersion("V1");   }
        else if (filename == "left_forearm_V2.txt")      { sP.setName(SkinPart_s[SKIN_LEFT_FOREARM]);  sP.setVersion("V2");   }
        else if (filename == "right_forearm_mesh.txt")   { sP.setName(SkinPart_s[SKIN_RIGHT_FOREARM]); sP.setVersion("V1");   }
        else if (filename == "right_forearm_nomesh.txt") { sP.setName(SkinPart_s[SKIN_RIGHT_FOREARM]); sP.setVersion("V1");   }
        else if (filename == "right_forearm_V2.txt")     { sP.setName(SkinPart_s[SKIN_RIGHT_FOREARM]); sP.setVersion("V2");   }
        else if (filename == "left_hand_V2_1.txt")       { sP.setName(SkinPart_s[SKIN_LEFT_HAND]);     sP.setVersion("V2.1"); }
        else if (filename == "right_hand_V2_1.txt")      { sP.setName(SkinPart_s[SKIN_RIGHT_HAND]);    sP.setVersion("V2.1"); }
        else
        {
            yError("[%s] Unexpected skin part file name: %s.\n",name.c_str(),filename.c_str());
            return false;
        }
        //filename = filename.substr(0, filename.find_last_of("_"));

        yarp::os::ResourceFinder rf;
        rf.setVerbose(false);
        rf.setDefaultContext("skinGui");            //overridden by --context parameter
        rf.setDefaultConfigFile(filePath.c_str()); //overridden by --from parameter
        if (!rf.configure(0,NULL))
        {
            yError("[%s] ResourceFinder was not configured correctly! Filename:",name.c_str());
            yError("%s",filename.c_str());
            return false;
        }
        rf.setVerbose(true);

        yarp::os::Bottle &calibration = rf.findGroup("calibration");
        if (calibration.isNull())
        {
            yError("[%s::setTaxelPosesFromFile] No calibration group found!",name.c_str());
            return false;
        }
//        printMessage(6,"[%s::setTaxelPosesFromFile] found %i taxels (not all of them are valid taxels).\n", name.c_str(),calibration.size()-1);

        sP.spatial_sampling = "triangle";
        // First item of the bottle is "calibration", so we should not use it
        int j = 0; //this will be for the taxel IDs
        for (int i = 1; i < calibration.size(); i++)
        {
            taxelPosNrm = vectorFromBottle(*(calibration.get(i).asList()),0,6);
            taxelPos = taxelPosNrm.subVector(0,2);
            taxelNrm = taxelPosNrm.subVector(3,5);
            j = i-1;   //! note that i == line in the calibration group of .txt file;  taxel_ID (j) == line nr (i) - 1
//            printMessage(10,"[vtRFThread::setTaxelPosesFromFile] reading %i th row: taxelPos: %s\n", i,taxelPos.toString(3,3).c_str());

            if (sP.name == SkinPart_s[SKIN_LEFT_FOREARM] || sP.name == SkinPart_s[SKIN_RIGHT_FOREARM])
            {
                // the taxels at the centers of respective triangles  -
                // e.g. first triangle of forearm arm is at lines 1-12, center at line 4, taxelID = 3
                if(  (j==3) || (j==15)  ||  (j==27) ||  (j==39) ||  (j==51) ||  (j==63) ||  (j==75) ||  (j==87) ||
                    (j==99) || (j==111) || (j==123) || (j==135) || (j==147) || (j==159) || (j==171) || (j==183) || //end of full lower patch
                    ((j==195) && (sP.version=="V2")) || (j==207) || ((j==231) && (sP.version=="V2")) || ((j==255) && (sP.version=="V1")) ||
                    ((j==267) && (sP.version=="V2")) || (j==291) || (j==303) || ((j==315) && (sP.version=="V1")) || (j==339) || (j==351) )

                // if(  (j==3) ||  (j==39) || (j==207) || (j==255) || (j==291)) // Taxels that are evenly distributed throughout the forearm
                                                                             // in order to cover it as much as we can
                // if(  (j==3) ||  (j==15) ||  (j==27) || (j==183)) // taxels that are in the big patch but closest to the little patch (internally)
                                                                    // 27 is proximal, 15 next, 3 next, 183 most distal
                // if((j==135) || (j==147) || (j==159) || (j==171))  // this is the second column, farther away from the stitch
                                                                     // 159 is most proximal, 147 is next, 135 next,  171 most distal
                // if((j==87) || (j==75)  || (j==39)|| (j==51)) // taxels that are in the big patch and closest to the little patch (externally)
                //                                              // 87 most proximal, 75 then, 39 then, 51 distal

                // if((j==27)  || (j==15)  || (j==3)   || (j==183) ||              // taxels used for the experimentations on the PLOS paper
                //    (j==135) || (j==147) || (j==159) || (j==171) ||
                //    (j==87)  || (j==75)  || (j==39)  || (j==51))

                // if((j==27)  || (j==15)  || (j==3)   || (j==183) ||              // taxels used for the experimentations on the IROS paper
                //    (j==87)  || (j==75)  || (j==39)  || (j==51))
                {
                    sP.size++;
                    if (modality=="1D")
                    {
                        sP.taxels.push_back(new TaxelPWE1D(taxelPos,taxelNrm,j));
                    }
                    else
                    {
                        sP.taxels.push_back(new TaxelPWE2D(taxelPos,taxelNrm,j));
                    }
                }
                else
                {
                    sP.size++;
                }
            }
            else if (sP.name == SkinPart_s[SKIN_LEFT_HAND])
            { //we want to represent the 48 taxels of the palm (ignoring fingertips) with 5 taxels -
             // manually marking 5 regions of the palm and selecting their "centroids" as the representatives
                if((j==99) || (j==101) || (j==109) || (j==122) || (j==134))
                {
                    sP.size++;
                    if (modality=="1D")
                    {
                        sP.taxels.push_back(new TaxelPWE1D(taxelPos,taxelNrm,j));
                    }
                    else
                    {
                        sP.taxels.push_back(new TaxelPWE2D(taxelPos,taxelNrm,j));
                    }
                }
                else
                {
                    sP.size++;
                }
            }
            else if (sP.name == SkinPart_s[SKIN_RIGHT_HAND])
            { //right hand has different taxel nr.s than left hand
                if((j==101) || (j==103) || (j==118) || (j==137) || (j==124))
                {
                    sP.size++;
                    if (modality=="1D")
                    {
                        sP.taxels.push_back(new TaxelPWE1D(taxelPos,taxelNrm,j));
                    }
                    else
                    {
                        sP.taxels.push_back(new TaxelPWE2D(taxelPos,taxelNrm,j));
                    }
                }
                else
                {
                    sP.size++;
                }
            }
        }

        initRepresentativeTaxels(sP);

        return true;
    }

    // From vtRFThread
    /**
    * If it is defined for the respective skin part, it fills the taxelIDtoRepresentativeTaxelID vector that is indexed by taxel IDs
    * and returns taxel IDs of their representatives - e.g. triangle centers.
    **/
    void initRepresentativeTaxels(skinPart &sP)
    {
        printMessage(6,"[vtRFThread::initRepresentativeTaxels] Initializing representative taxels for %s, version %s\n",sP.name.c_str(),sP.version.c_str());
        int j=0; //here j will start from 0 and correspond to taxel ID
        list<unsigned int> taxels_list;
        if (sP.name == SkinPart_s[SKIN_LEFT_FOREARM] || sP.name == SkinPart_s[SKIN_RIGHT_FOREARM])
        {
            for (j=0;j<sP.size;j++)
            {
                //4th taxel of each 12, but with ID 3, j starting from 0 here, is the triangle midpoint
                sP.taxel2Repr.push_back(((j/12)*12)+3); //initialize all 384 taxels with triangle center as the representative
                //fill a map of lists here somehow
            }

            // set to -1 the taxel2Repr for all the taxels that don't exist
            if (sP.version == "V1")
            {
                for (j=192;j<=203;j++)
                {
                    sP.taxel2Repr[j]=-1; //these taxels don't exist
                }
            }
            for (j=216;j<=227;j++)
            {
                sP.taxel2Repr[j]=-1; //these taxels don't exist
            }
            if (sP.version == "V1")
            {
                for (j=228;j<=239;j++)
                {
                    sP.taxel2Repr[j]=-1; //these taxels don't exist
                }
            }
            for (j=240;j<=251;j++)
            {
                sP.taxel2Repr[j]=-1; //these taxels don't exist
            }
            if (sP.version == "V2")
            {
                for (j=252;j<=263;j++)
                {
                    sP.taxel2Repr[j]=-1; //these taxels don't exist
                }
            }
            if (sP.version == "V1")
            {
                for (j=264;j<=275;j++)
                {
                    sP.taxel2Repr[j]=-1; //these taxels don't exist
                }
            }
            for (j=276;j<=287;j++)
            {
                sP.taxel2Repr[j]=-1; //these taxels don't exist
            }
            if (sP.version == "V2")
            {
                for (j=312;j<=323;j++)
                {
                    sP.taxel2Repr[j]=-1; //these taxels don't exist
                }
            }
            for (j=324;j<=335;j++)
            {
                sP.taxel2Repr[j]=-1; //these taxels don't exist
            }
            for (j=360;j<=383;j++)
            {
                sP.taxel2Repr[j]=-1; //these taxels don't exist
            }

            // Set up the inverse - from every representative taxel to list of taxels it is representing
            taxels_list.clear();
            for(j=0;j<=11;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[3] = taxels_list;

            taxels_list.clear();
            for(j=12;j<=23;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[15] = taxels_list;

            taxels_list.clear();
            for(j=24;j<=35;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[27] = taxels_list;

            taxels_list.clear();
            for(j=36;j<=47;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[39] = taxels_list;

            taxels_list.clear();
            for(j=48;j<=59;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[51] = taxels_list;

            taxels_list.clear();
            for(j=60;j<=71;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[63] = taxels_list;

            taxels_list.clear();
            for(j=72;j<=83;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[75] = taxels_list;

            taxels_list.clear();
            for(j=84;j<=95;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[87] = taxels_list;

            taxels_list.clear();
            for(j=96;j<=107;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[99] = taxels_list;

            taxels_list.clear();
            for(j=108;j<=119;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[111] = taxels_list;

            taxels_list.clear();
            for(j=120;j<=131;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[123] = taxels_list;

            taxels_list.clear();
            for(j=132;j<=143;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[135] = taxels_list;

            taxels_list.clear();
            for(j=144;j<=155;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[147] = taxels_list;

            taxels_list.clear();
            for(j=156;j<=167;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[159] = taxels_list;

            taxels_list.clear();
            for(j=168;j<=179;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[171] = taxels_list;

            taxels_list.clear();
            for(j=180;j<=191;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[183] = taxels_list;
            //up to here - lower (full) patch on forearm

            //from here - upper patch with many dummy positions on the port - incomplete patch
            if (sP.version == "V2")
            {
                taxels_list.clear();
                for(j=192;j<=203;j++)
                {
                    taxels_list.push_back(j);
                }
                sP.repr2TaxelList[195] = taxels_list;
            }

            taxels_list.clear();
            for(j=204;j<=215;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[207] = taxels_list;

            if (sP.version == "V2")
            {
                taxels_list.clear();
                for(j=228;j<=237;j++)
                {
                    taxels_list.push_back(j);
                }
                sP.repr2TaxelList[231] = taxels_list;
            }

            if (sP.version == "V1")
            {
                taxels_list.clear();
                for(j=252;j<=263;j++)
                {
                    taxels_list.push_back(j);
                }
                sP.repr2TaxelList[255] = taxels_list;
            }

            if (sP.version == "V2")
            {
                taxels_list.clear();
                for(j=264;j<=275;j++)
                {
                    taxels_list.push_back(j);
                }
                sP.repr2TaxelList[267] = taxels_list;
            }

            taxels_list.clear();
            for(j=288;j<=299;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[291] = taxels_list;

            taxels_list.clear();
            for(j=300;j<=311;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[303] = taxels_list;

            if (sP.version == "V1")
            {
                taxels_list.clear();
                for(j=312;j<=323;j++)
                {
                    taxels_list.push_back(j);
                }
                sP.repr2TaxelList[315] = taxels_list;
            }

            taxels_list.clear();
            for(j=336;j<=347;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[339] = taxels_list;

            taxels_list.clear();
            for(j=348;j<=359;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[351] = taxels_list;
        }
        else if(sP.name == SkinPart_s[SKIN_LEFT_HAND])
        {
            for(j=0;j<sP.size;j++)
            {
                // Fill all the 192 with -1 - half of the taxels don't exist,
                // and for fingertips we don't have positions either
                sP.taxel2Repr.push_back(-1);
            }

            // Upper left area of the palm - at thumb
            for (j=121;j<=128;j++)
            {
                sP.taxel2Repr[j] = 122;
            }
            sP.taxel2Repr[131] = 122; //thermal pad

            // Set up the mapping in the other direction - from every representative taxel to list of taxels it is representing
            taxels_list.clear();
            for(j=121;j<=128;j++)
            {
                taxels_list.push_back(j);
            }
            taxels_list.push_back(131);
            sP.repr2TaxelList[122] = taxels_list;

            // Upper center of the palm
            for (j=96;j<=99;j++)
            {
                sP.taxel2Repr[j] = 99;
            }
            sP.taxel2Repr[102] = 99;
            sP.taxel2Repr[103] = 99;
            sP.taxel2Repr[120] = 99;
            sP.taxel2Repr[129] = 99;
            sP.taxel2Repr[130] = 99;

            // Set up the mapping in the other direction - from every representative taxel to list of taxels it is representing
            taxels_list.clear();
            for(j=96;j<=99;j++)
            {
                taxels_list.push_back(j);
            }
            taxels_list.push_back(102);
            taxels_list.push_back(103);
            taxels_list.push_back(120);
            taxels_list.push_back(129);
            taxels_list.push_back(130);
            sP.repr2TaxelList[99] = taxels_list;

            // Upper right of the palm (away from the thumb)
            sP.taxel2Repr[100] = 101;
            sP.taxel2Repr[101] = 101;
            for (j=104;j<=107;j++)
            {
                sP.taxel2Repr[j] = 101; //N.B. 107 is thermal pad
            }
            sP.taxel2Repr[113] = 101;
            sP.taxel2Repr[116] = 101;
            sP.taxel2Repr[117] = 101;

            // Set up the mapping in the other direction - from every representative taxel to list of taxels it is representing
            taxels_list.clear();
            taxels_list.push_back(100);
            taxels_list.push_back(101);
            for(j=104;j<=107;j++)
            {
                taxels_list.push_back(j);
            }
            taxels_list.push_back(113);
            taxels_list.push_back(116);
            taxels_list.push_back(117);
            sP.repr2TaxelList[101] = taxels_list;

            // Center area of the palm
            for(j=108;j<=112;j++)
            {
                sP.taxel2Repr[j] = 109;
            }
            sP.taxel2Repr[114] = 109;
            sP.taxel2Repr[115] = 109;
            sP.taxel2Repr[118] = 109;
            sP.taxel2Repr[142] = 109;
            sP.taxel2Repr[143] = 109;

            // Set up the mapping in the other direction - from every representative taxel to list of taxels it is representing
            taxels_list.clear();
            for(j=108;j<=112;j++)
            {
                taxels_list.push_back(j);
            }
            taxels_list.push_back(114);
            taxels_list.push_back(115);
            taxels_list.push_back(118);
            taxels_list.push_back(142);
            taxels_list.push_back(143);
            sP.repr2TaxelList[109] = taxels_list;

            // Lower part of the palm
            sP.taxel2Repr[119] = 134; // this one is thermal pad
            for(j=132;j<=141;j++)
            {
                sP.taxel2Repr[j] = 134;
            }

            // Set up the mapping in the other direction - from every representative taxel to list of taxels it is representing
            taxels_list.clear();
            taxels_list.push_back(119);
            for(j=132;j<=141;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[134] = taxels_list;
        }
        else if(sP.name == SkinPart_s[SKIN_RIGHT_HAND])
        {
           for(j=0;j<sP.size;j++)
           {
              sP.taxel2Repr.push_back(-1); //let's fill all the 192 with -1 - half of the taxels don't exist and for fingertips,
              //we don't have positions either
           }
           //upper left area - away from thumb on this hand
            sP.taxel2Repr[96] = 101;
            sP.taxel2Repr[97] = 101;
            sP.taxel2Repr[98] = 101;
            sP.taxel2Repr[100] = 101;
            sP.taxel2Repr[101] = 101;
            sP.taxel2Repr[107] = 101; //thermal pad
            sP.taxel2Repr[110] = 101;
            sP.taxel2Repr[111] = 101;
            sP.taxel2Repr[112] = 101;

            // Set up the mapping in the other direction - from every representative taxel to list of taxels it is representing
            taxels_list.clear();
            taxels_list.push_back(96);
            taxels_list.push_back(97);
            taxels_list.push_back(98);
            taxels_list.push_back(100);
            taxels_list.push_back(101);
            taxels_list.push_back(107);
            taxels_list.push_back(110);
            taxels_list.push_back(111);
            taxels_list.push_back(112);
            sP.repr2TaxelList[101] = taxels_list;

            //upper center of the palm
            sP.taxel2Repr[99] = 103;
            for(j=102;j<=106;j++)
            {
               sP.taxel2Repr[j] = 103;
            }
            sP.taxel2Repr[127] = 103;
            sP.taxel2Repr[129] = 103;
            sP.taxel2Repr[130] = 103;

            // Set up the mapping in the other direction - from every representative taxel to list of taxels it is representing
            taxels_list.clear();
            taxels_list.push_back(99);
            for(j=102;j<=106;j++)
            {
                taxels_list.push_back(j);
            }
            taxels_list.push_back(127);
            taxels_list.push_back(129);
            taxels_list.push_back(130);
            sP.repr2TaxelList[103] = taxels_list;


            //upper right center of the palm - at thumb
            for(j=120;j<=126;j++)
            {
               sP.taxel2Repr[j] = 124;
            }
            sP.taxel2Repr[128] = 124;
            sP.taxel2Repr[131] = 124; //thermal pad

            // Set up the mapping in the other direction - from every representative taxel to list of taxels it is representing
            taxels_list.clear();
            for(j=120;j<=126;j++)
            {
                taxels_list.push_back(j);
            }
            taxels_list.push_back(128);
            taxels_list.push_back(131);
            sP.repr2TaxelList[124] = taxels_list;


            //center of palm
            sP.taxel2Repr[108] = 118;
            sP.taxel2Repr[109] = 118;
            for(j=113;j<=118;j++)
            {
                sP.taxel2Repr[j] = 118;
            }
            sP.taxel2Repr[142] = 118;
            sP.taxel2Repr[143] = 118;

            // Set up the mapping in the other direction - from every representative taxel to list of taxels it is representing
            taxels_list.clear();
            taxels_list.push_back(108);
            taxels_list.push_back(109);
            for(j=113;j<=118;j++)
            {
                taxels_list.push_back(j);
            }
            taxels_list.push_back(142);
            taxels_list.push_back(143);
            sP.repr2TaxelList[118] = taxels_list;

            //lower palm
            sP.taxel2Repr[119] = 137; //thermal pad
            for(j=132;j<=141;j++)
            {
                sP.taxel2Repr[j] = 137; //139 is another thermal pad
            }

            // Set up the mapping in the other direction - from every representative taxel to list of taxels it is representing
            taxels_list.clear();
            taxels_list.push_back(119);
            for(j=132;j<=141;j++)
            {
                taxels_list.push_back(j);
            }
            sP.repr2TaxelList[137] = taxels_list;
        }
    }

    // From vtRFThread
    /**
    * For all active taxels, it returns a set of "representative" active taxels if they were defined for the respective skin part
    * E.g. if at least one taxel from a triangle was active, the center of this triangle will be part of the output list
    * @param IDv  is a vector of IDs of the taxels activated
    * @param IDx  is the index of the iCubSkin affected by the contact
                  (basically, the index of the skinPart that has been touched)
    * @param v    is a vector of IDs of the representative taxels activated
    **/
    bool getRepresentativeTaxels(const std::vector<unsigned int> IDv, const int IDx, std::vector<unsigned int> &v)
    {
        //unordered_set would be better, but that is only experimentally supported by some compilers.
        std::set<unsigned int> rep_taxel_IDs_set;

        if (iCubSkin[IDx].taxel2Repr.empty())
        {
            v = IDv; //we simply copy the activated taxels
            return false;
        }
        else
        {
            for (std::vector<unsigned int>::const_iterator it = IDv.begin() ; it != IDv.end(); ++it)
            {
                if (iCubSkin[IDx].taxel2Repr[*it] == -1)
                {
                    yWarning("[%s] taxel %u activated, but representative taxel undefined - ignoring.",iCubSkin[IDx].name.c_str(),*it);
                }
                else
                {
                    rep_taxel_IDs_set.insert(iCubSkin[IDx].taxel2Repr[*it]); //add all the representatives that were activated to the set
                }
            }

            for (std::set<unsigned int>::const_iterator itr = rep_taxel_IDs_set.begin(); itr != rep_taxel_IDs_set.end(); ++itr)
            {
                v.push_back(*itr); //add the representative taxels that were activated to the output taxel ID vector
            }

            if (v.empty())
            {
                yWarning("Representative taxels' vector is empty! Skipping.");
                return false;
            }

            if (verbosity>=4)
            {
                printMessage(4,"Representative taxels on skin part %d: ",IDx);
                for(std::vector<unsigned int>::const_iterator it = v.begin() ; it != v.end(); ++it)
                {
                    printf("%d ",*it);
                }
                printf("\n");
            }
        }

        return true;
    }

    bool readEncodersAndUpdateArmChains()
    {
       Vector q1(jntsT+jntsAR,0.0);
       Vector q2(jntsT+jntsAL,0.0);

       iencsT->getEncoders(encsT->data());
       qT[0]=(*encsT)[2]; //reshuffling from motor to iKin order (yaw, roll, pitch)
       qT[1]=(*encsT)[1];
       qT[2]=(*encsT)[0];

       if (armsRF.check("rightHand") || armsRF.check("rightForeArm") ||
            (!armsRF.check("rightHand") && !armsRF.check("rightForeArm") && !armsRF.check("leftHand") && !armsRF.check("leftForeArm")))
       {
            iencsR->getEncoders(encsR->data());
            qR=encsR->subVector(0,jntsAR-1);
            q1.setSubvector(0,qT);
            q1.setSubvector(jntsT,qR);
            armR -> setAng(q1*CTRL_DEG2RAD);
       }
       if (armsRF.check("leftHand") || armsRF.check("leftForeArm") ||
               (!armsRF.check("rightHand") && !armsRF.check("rightForeArm") && !armsRF.check("leftHand") && !armsRF.check("leftForeArm")))
       {
            iencsL->getEncoders(encsL->data());
            qL=encsL->subVector(0,jntsAL-1);
            q2.setSubvector(0,qT);
            q2.setSubvector(jntsT,qL);
            armL -> setAng(q2*CTRL_DEG2RAD);
       }

       return true;
    }
/*
    bool readHeadEncodersAndUpdateEyeChains()
    {
        iencsH->getEncoders(encsH->data());
        yarp::sig::Vector  head=*encsH;

        yarp::sig::Vector q(8);
        q[0]=qT[0];       q[1]=qT[1];        q[2]=qT[2];
        q[3]=head[0];        q[4]=head[1];
        q[5]=head[2];        q[6]=head[3];

        //left eye
        q[7]=head[4]+head[5]/2.0;
        eWL->eye->setAng(q*CTRL_DEG2RAD);
        //right eye
        q[7]=head[4]-head[5]/2.0;
        eWR->eye->setAng(q*CTRL_DEG2RAD);

        return true;
    }
*/

    yarp::sig::Vector locateTaxel(const yarp::sig::Vector &_pos, const string &part)
    {
        yarp::sig::Vector pos=_pos;
        yarp::sig::Vector WRFpos(4,0.0);
        Matrix T = eye(4);

//        printMessage(7,"locateTaxel(): Pos local frame: %s, skin part name: %s\n",_pos.toString(3,3).c_str(),part.c_str());
        if (!((part == SkinPart_s[SKIN_LEFT_FOREARM]) || (part == SkinPart_s[SKIN_LEFT_HAND]) ||
             (part == SkinPart_s[SKIN_RIGHT_FOREARM]) || (part == SkinPart_s[SKIN_RIGHT_HAND])))
            yError("[%s] locateTaxel() failed - unknown skinPart!\n",name.c_str());

        if      (part == SkinPart_s[SKIN_LEFT_FOREARM] ) { T = armL -> getH(3+4, true); } // torso + up to elbow
        else if (part == SkinPart_s[SKIN_RIGHT_FOREARM]) { T = armR -> getH(3+4, true); } // torso + up to elbow
        else if (part == SkinPart_s[SKIN_LEFT_HAND])     { T = armL -> getH(3+6, true); } // torso + up to wrist
        else if (part == SkinPart_s[SKIN_RIGHT_HAND])    { T = armR -> getH(3+6, true); } // torso + up to wrist
        else    {  yError("[%s] locateTaxel() failed!\n",name.c_str()); }

//        printMessage(8,"    T Matrix: \n %s \n ",T.toString(3,3).c_str());
        pos.push_back(1);
        WRFpos = T * pos;
        WRFpos.pop_back();

        return WRFpos;
    }

    // From vtRFThread
    int printMessage(const int l, const char *f, ...) const
    {
        if (verbosity>=l)
        {
            fprintf(stdout,"[%s] ",name.c_str());

            va_list ap;
            va_start(ap,f);
            int ret=vfprintf(stdout,f,ap);
            va_end(ap);

            return ret;
        }
        else
            return -1;
    }

    void vector2bottle(const std::vector<Vector> &vec, yarp::os::Bottle &b)
    {
        for (int16_t i=0; i<vec.size(); i++)
        {
            for (int8_t j=0; j<vec[i].size(); j++)
                b.addDouble(vec[i][j]);
        }
    }

public:
    //********************************************
    bool configure(ResourceFinder &rf)
    {
        name        =rf.check("name",Value("visuoTactileCalib")).asString().c_str();
        robot       =rf.check("robot",Value("icub")).asString().c_str();
        period      =rf.check("period",Value(0.0)).asDouble();
        modality    =rf.check("name",Value("1D")).asString().c_str();

        if(robot == "icub")
            arm_version = 2.0;
        else //icubSim
            arm_version = 1.0;

        skinPortIn.open(("/"+name+"/skin_events:i").c_str());
//        skinPortIn.setReader(*this);
        if (Network::connect("/skinManager/skin_events:o",("/"+name+"/skin_events:i").c_str()))
            yInfo("[%s] Connected /skinManager/skin_events:o to %s successful",name.c_str(), skinPortIn.getName().c_str());
        else
            yWarning("[%s] Cannot connect /skinManager/skin_events:o to %s!!!",name.c_str(), skinPortIn.getName().c_str());

        contactDumperPortOut.open(("/"+name+"/contactPtsDumper:o").c_str());

        //******************* ARMS, EYEWRAPPERS ******************

        std::ostringstream strR;
        strR<<"right_v"<<arm_version;
        std::string typeR = strR.str();
        armR = new iCubArm(typeR);

        std::ostringstream strL;
        strL<<"left_v"<<arm_version;
        std::string typeL = strL.str();
        armL = new iCubArm(typeL);

        ts.update();

        jntsT = 3; //nr torso joints
        /********** Open right arm interfaces (if they are needed) ***************/
        bool ok = true;

        armsRF.setVerbose(false);
        armsRF.setDefaultContext("periPersonalSpace");
//        armsRF.setDefaultConfigFile("skinManAll.ini");
        armsRF.configure(0,NULL);

        if (armsRF.check("rightHand") || armsRF.check("rightForeArm") ||
           (!armsRF.check("rightHand") && !armsRF.check("rightForeArm") && !armsRF.check("leftHand") && !armsRF.check("leftForeArm")))
        {
            for (int i = 0; i < jntsT; i++)
                armR->releaseLink(i); //torso will be enabled
            Property OptR;
            OptR.put("robot",  robot.c_str());
            OptR.put("part",   "right_arm");
            OptR.put("device", "remote_controlboard");
            OptR.put("remote",("/"+robot+"/right_arm").c_str());
            OptR.put("local", ("/"+name +"/right_arm").c_str());

            if (!ddR.open(OptR))
            {
                yError("[%s] : could not open right_arm PolyDriver!\n",name.c_str());
                return false;
            }
            ok = 1;
            if (ddR.isValid())
            {
                ok = ok && ddR.view(iencsR);
            }
            if (!ok)
            {
                yError("[%s] Problems acquiring right_arm interfaces!!!!\n",name.c_str());
                return false;
            }
            iencsR->getAxes(&jntsR);
            encsR = new yarp::sig::Vector(jntsR,0.0); //should be 16 - arm + fingers
            jntsAR = 7; //nr. arm joints only - without fingers
            qR.resize(jntsAR,0.0); //current values of arm joints (should be 7)

        }

        /********** Open left arm interfaces (if they are needed) ****************/
        if (armsRF.check("leftHand") || armsRF.check("leftForeArm") ||
           (!armsRF.check("rightHand") && !armsRF.check("rightForeArm") && !armsRF.check("leftHand") && !armsRF.check("leftForeArm")))
        {
            for (int i = 0; i < jntsT; i++)
                armL->releaseLink(i); //torso will be enabled
            Property OptL;
            OptL.put("robot",  robot.c_str());
            OptL.put("part",   "left_arm");
            OptL.put("device", "remote_controlboard");
            OptL.put("remote",("/"+robot+"/left_arm").c_str());
            OptL.put("local", ("/"+name +"/left_arm").c_str());

            if (!ddL.open(OptL))
            {
                yError("[%s] : could not open left_arm PolyDriver!\n",name.c_str());
                return false;
            }
            ok = 1;
            if (ddL.isValid())
            {
                ok = ok && ddL.view(iencsL);
            }
            if (!ok)
            {
                yError("[%s] Problems acquiring left_arm interfaces!!!!\n",name.c_str());
                return false;
            }
            iencsL->getAxes(&jntsL);
            encsL = new yarp::sig::Vector(jntsL,0.0); //should be 16 - arm + fingers
            jntsAL = 7; //nr. arm joints only - without fingers
            qL.resize(jntsAL,0.0); //current values of arm joints (should be 7)

        }

        /**************************/
        Property OptT;
        OptT.put("robot",  robot.c_str());
        OptT.put("part",   "torso");
        OptT.put("device", "remote_controlboard");
        OptT.put("remote",("/"+robot+"/torso").c_str());
        OptT.put("local", ("/"+name +"/torso").c_str());

        if (!ddT.open(OptT))
        {
            yError("[%s] Could not open torso PolyDriver!",name.c_str());
            return false;
        }
        ok = 1;
        if (ddT.isValid())
        {
            ok = ok && ddT.view(iencsT);
        }
        if (!ok)
        {
            yError("[%s] Problems acquiring head interfaces!!!!",name.c_str());
            return false;
        }
        iencsT->getAxes(&jntsT);
        encsT = new yarp::sig::Vector(jntsT,0.0);
        qT.resize(jntsT,0.0); //current values of torso joints (3, in the order expected for iKin: yaw, roll, pitch)


        //************* skinManager Resource finder **************
        ResourceFinder skinRF;
        skinRF.setVerbose(false);
        skinRF.setDefaultContext("skinGui");                //overridden by --context parameter
        skinRF.setDefaultConfigFile("skinManAll.ini");      //overridden by --from parameter
        skinRF.configure(0,NULL);

        //int partNum=4;

        Bottle &skinEventsConf = skinRF.findGroup("SKIN_EVENTS");
        if(!skinEventsConf.isNull())
        {
            yInfo("SKIN_EVENTS section found\n");

            // the code below relies on a fixed order of taxel pos files in skinManAll.ini
            if(skinEventsConf.check("taxelPositionFiles"))
            {
                Bottle *taxelPosFiles = skinEventsConf.find("taxelPositionFiles").asList();

                if (rf.check("leftHand") || rf.check("leftForeArm") || rf.check("rightHand") || rf.check("rightForeArm"))
                {
                    if (rf.check("leftHand"))
                    {
                        string taxelPosFile = taxelPosFiles->get(0).asString().c_str();
                        string filePath(skinRF.findFile(taxelPosFile.c_str()));
                        if (filePath!="")
                        {
                            yInfo("[%s] filePath leftHand: %s\n",name.c_str(),filePath.c_str());
                            filenames.push_back(filePath);
                        }
                    }
                    if (rf.check("leftForeArm"))
                    {
                        string taxelPosFile = taxelPosFiles->get(1).asString().c_str();
                        string filePath(skinRF.findFile(taxelPosFile.c_str()));
                        if (filePath!="")
                        {
                            yInfo("[%s] filePath leftForeArm: %s\n",name.c_str(),filePath.c_str());
                            filenames.push_back(filePath);
                        }
                    }
                    if (rf.check("rightHand"))
                    {
                        string taxelPosFile = taxelPosFiles->get(3).asString().c_str();
                        string filePath(skinRF.findFile(taxelPosFile.c_str()));
                        if (filePath!="")
                        {
                            yInfo("[%s] filePath rightHand: %s\n",name.c_str(),filePath.c_str());
                            filenames.push_back(filePath);
                        }
                    }
                    if (rf.check("rightForeArm"))
                    {
                        string taxelPosFile = taxelPosFiles->get(4).asString().c_str();
                        string filePath(skinRF.findFile(taxelPosFile.c_str()));
                        if (filePath!="")
                        {
                            yInfo("[%s] filePath rightForeArm: %s\n",name.c_str(),filePath.c_str());
                            filenames.push_back(filePath);
                        }
                    }
                }
                else
                {
                    string taxelPosFile = taxelPosFiles->get(0).asString().c_str();
                    string filePath(skinRF.findFile(taxelPosFile.c_str()));
                    if (filePath!="")
                    {
                        yInfo("[%s] filePath leftHand: %s\n",name.c_str(),filePath.c_str());
                        filenames.push_back(filePath);
                    }
                    taxelPosFile.clear(); filePath.clear();

                    taxelPosFile = taxelPosFiles->get(1).asString().c_str();
                    filePath = skinRF.findFile(taxelPosFile.c_str());
                    if (filePath!="")
                    {
                        yInfo("[%s] filePath leftForeArm: %s\n",name.c_str(),filePath.c_str());
                        filenames.push_back(filePath);
                    }
                    taxelPosFile.clear(); filePath.clear();

                    taxelPosFile = taxelPosFiles->get(3).asString().c_str();
                    filePath = skinRF.findFile(taxelPosFile.c_str());
                    if (filePath!="")
                    {
                        yInfo("[%s] filePath rightHand: %s\n",name.c_str(),filePath.c_str());
                        filenames.push_back(filePath);
                    }
                    taxelPosFile.clear(); filePath.clear();

                    taxelPosFile = taxelPosFiles->get(4).asString().c_str();
                    filePath = skinRF.findFile(taxelPosFile.c_str());
                    if (filePath!="")
                    {
                        yInfo("[%s] filePath rightForeArm: %s\n",name.c_str(),filePath.c_str());
                        filenames.push_back(filePath);
                    }
                }
            }
        }
        else
        {
            yError("[%s] No skin configuration files found.",name.c_str());
            return 0;
        }

        yDebug("[%s] Setting up iCubSkin...",name.c_str());
        iCubSkinSize = filenames.size();

        for(unsigned int i=0;i<filenames.size();i++)
        {
            string filePath = filenames[i];
            yDebug("i: %i filePath: %s",i,filePath.c_str());
            skinPartPWE sP(modality);
            if (setTaxelPosesFromFile(filePath,sP) )
            {
                iCubSkin.push_back(sP);
            }
        }

//        rpcSrvr.open(("/"+name+"/rpc:i").c_str());
//        attach(rpcSrvr);

        return true;
    }

    //********************************************
    bool respond(const Bottle &command, Bottle &reply)
    {
        int ack =Vocab::encode("ack");
        int nack=Vocab::encode("nack");

        return true;
    }

    //********************************************
    bool updateModule()
    {
        ts.update();
        // read skin contact
        skinContactList *skinContacts  = skinPortIn.read(false);

        // update the kinematic chain wrt World Reference Frame
        readEncodersAndUpdateArmChains();
//        readHeadEncodersAndUpdateEyeChains(); //has to be called after readEncodersAndUpdateArmChains(), which reads torso encoders

        // project taxels in World Reference Frame
        for (size_t i = 0; i < iCubSkin.size(); i++)
        {
            for (size_t j = 0; j < iCubSkin[i].taxels.size(); j++)
            {
                iCubSkin[i].taxels[j]->setWRFPosition(locateTaxel(iCubSkin[i].taxels[j]->getPosition(),iCubSkin[i].name));
            }
        }

        if (skinContacts)
        {
            std::vector<unsigned int> IDv; IDv.clear();
            int IDx = -1;
            contactPts.clear();
            if (detectContact(skinContacts, IDx, IDv))
            {
                yInfo("[%s] Contact! Collect tactile data..",name.c_str());
                timeNow     = yarp::os::Time::now();
            }
        }

        if (contactPts.size()>0)
        {
            Bottle contactBottle;
            contactBottle.clear();

            vector2bottle(contactPts, contactBottle);
            contactDumperPortOut.setEnvelope(ts);
            contactDumperPortOut.write(contactBottle);
        }

        return true;
    }

    //********************************************
    double getPeriod()
    {
        return period;
    }

    //********************************************
    bool close()
    {
        yInfo("[%s] Closing module..",name.c_str());

        skinPortIn.close();
        yDebug("[%s]Closing controllers..\n",name.c_str());
        ddR.close();
        ddL.close();
        ddT.close();
        ddH.close();

        yDebug("[%s]Deleting misc stuff..\n",name.c_str());
        delete armR;
        armR = NULL;
        delete armL;
        armL = NULL;

        return true;

    }
};


//********************************************
int main(int argc, char * argv[])
{
    Network yarp;

    ResourceFinder moduleRF;
    moduleRF.setVerbose(false);
    moduleRF.setDefaultContext("skeleton3D");
    moduleRF.setDefaultConfigFile("visuoTactileCalib.ini");
    moduleRF.configure(argc,argv);

    if (moduleRF.check("help"))
    {
        yInfo(" ");
        yInfo("Options:");
        yInfo("   --context     path:  where to find the called resource (default skeleton3D).");
        yInfo("   --from        from:  the name of the .ini file (default yetAnotherAvoidance.ini).");
        yInfo("   --name        name:  the name of the module (default yetAnotherAvoidance).");
        yInfo(" ");
        return 0;
    }

    if (!yarp.checkNetwork())
    {
        yError("No Network!!!");
        return -1;
    }

    visuoTactileCalib calibrator;
    return calibrator.runModule(moduleRF);
}
