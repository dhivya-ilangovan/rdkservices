/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
*
* Copyright 2019 RDK Management
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
**/

#include "HdmiInput.h"
#include "UtilsJsonRpc.h"
#include "UtilsIarm.h"

#include "hdmiIn.hpp"
#include "exception.hpp"
#include "dsUtl.h"
#include "dsError.h"
#include "dsMgr.h"
#include <math.h>

#include <vector>
#include <algorithm>

#define HDMI_HOT_PLUG_EVENT_CONNECTED 0
#define HDMI_HOT_PLUG_EVENT_DISCONNECTED 1

#define HDMIINPUT_METHOD_GET_HDMI_INPUT_DEVICES "getHDMIInputDevices"
#define HDMIINPUT_METHOD_WRITE_EDID "writeEDID"
#define HDMIINPUT_METHOD_READ_EDID "readEDID"
#define HDMIINPUT_METHOD_READ_RAWHDMISPD "getRawHDMISPD"
#define HDMIINPUT_METHOD_READ_HDMISPD "getHDMISPD"
#define HDMIINPUT_METHOD_SET_EDID_VERSION "setEdidVersion"
#define HDMIINPUT_METHOD_GET_EDID_VERSION "getEdidVersion"
#define HDMIINPUT_METHOD_START_HDMI_INPUT "startHdmiInput"
#define HDMIINPUT_METHOD_STOP_HDMI_INPUT "stopHdmiInput"
#define HDMIINPUT_METHOD_SCALE_HDMI_INPUT "setVideoRectangle"
#define HDMIINPUT_METHOD_SUPPORTED_GAME_FEATURES "getSupportedGameFeatures"
#define HDMIINPUT_METHOD_GAME_FEATURE_STATUS "getHdmiGameFeatureStatus"

#define HDMIINPUT_EVENT_ON_DEVICES_CHANGED "onDevicesChanged"
#define HDMIINPUT_EVENT_ON_SIGNAL_CHANGED "onSignalChanged"
#define HDMIINPUT_EVENT_ON_STATUS_CHANGED "onInputStatusChanged"
#define HDMIINPUT_EVENT_ON_VIDEO_MODE_UPDATED "videoStreamInfoUpdate"
#define HDMIINPUT_EVENT_ON_GAME_FEATURE_STATUS_CHANGED "hdmiGameFeatureStatusUpdate"

// TODO: remove this
#define registerMethod(...) for (uint8_t i = 1; GetHandler(i); i++) GetHandler(i)->Register<JsonObject, JsonObject>(__VA_ARGS__)

#define API_VERSION_NUMBER_MAJOR 1
#define API_VERSION_NUMBER_MINOR 0
#define API_VERSION_NUMBER_PATCH 1

#define PLUGIN_PROFILE_START() \
    struct timespec start; \
    struct timespec stop; \
    clock_gettime(CLOCK_MONOTONIC, &start);\
	LOGINFO("%s: Entry\n",__FUNCTION__);

#define PLUGIN_PROFILE_END() \
    clock_gettime(CLOCK_MONOTONIC, &stop); \
    LOGINFO("%s : Exit Time: %ldms\n", __FUNCTION__, getElapsedTimeMs(&start, &stop));
	
#undef returnIfParamNotFound

#define returnIfParamNotFound(param, name) \
    if (!param.HasLabel(name)) \
    { \
        LOGERR("No argument '%s'", name); \
		PLUGIN_PROFILE_END(); \
        returnResponse(false); \
    }

long getElapsedTimeMs(struct timespec *start, struct timespec *stop)
{
    struct timespec elapsed = {};
    if ((stop->tv_nsec - start->tv_nsec) < 0)
    {
        elapsed.tv_sec = stop->tv_sec - start->tv_sec - 1;
        elapsed.tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
    }
    else
    {
        elapsed.tv_sec = stop->tv_sec - start->tv_sec;
        elapsed.tv_nsec = stop->tv_nsec - start->tv_nsec;
    }
    return (elapsed.tv_sec*1000 + lround(elapsed.tv_nsec/1e6));
}

using namespace std;

namespace WPEFramework
{
    namespace {

        static Plugin::Metadata<Plugin::HdmiInput> metadata(
            // Version (Major, Minor, Patch)
            API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH,
            // Preconditions
            {},
            // Terminations
            {},
            // Controls
            {}
        );
    }

    namespace Plugin
    {
        SERVICE_REGISTRATION(HdmiInput, API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH);

        HdmiInput* HdmiInput::_instance = nullptr;

        HdmiInput::HdmiInput()
        : PluginHost::JSONRPC()
        {
            HdmiInput::_instance = this;

            //InitializeIARM();

            CreateHandler({2});

            registerMethod(HDMIINPUT_METHOD_GET_HDMI_INPUT_DEVICES, &HdmiInput::getHDMIInputDevicesWrapper, this);
            registerMethod(HDMIINPUT_METHOD_WRITE_EDID, &HdmiInput::writeEDIDWrapper, this);
            registerMethod(HDMIINPUT_METHOD_READ_EDID, &HdmiInput::readEDIDWrapper, this);
            registerMethod(HDMIINPUT_METHOD_READ_RAWHDMISPD, &HdmiInput::getRawHDMISPDWrapper, this);
            registerMethod(HDMIINPUT_METHOD_READ_HDMISPD, &HdmiInput::getHDMISPDWrapper, this);
            registerMethod(HDMIINPUT_METHOD_SET_EDID_VERSION, &HdmiInput::setEdidVersionWrapper, this);
            registerMethod(HDMIINPUT_METHOD_GET_EDID_VERSION, &HdmiInput::getEdidVersionWrapper, this);
            registerMethod(HDMIINPUT_METHOD_START_HDMI_INPUT, &HdmiInput::startHdmiInput, this);
            registerMethod(HDMIINPUT_METHOD_STOP_HDMI_INPUT, &HdmiInput::stopHdmiInput, this);
            registerMethod(HDMIINPUT_METHOD_SCALE_HDMI_INPUT, &HdmiInput::setVideoRectangleWrapper, this);

            registerMethod(HDMIINPUT_METHOD_SUPPORTED_GAME_FEATURES, &HdmiInput::getSupportedGameFeatures, this);
            registerMethod(HDMIINPUT_METHOD_GAME_FEATURE_STATUS, &HdmiInput::getHdmiGameFeatureStatusWrapper, this);
        }

        HdmiInput::~HdmiInput()
        {
        }
	    
	const string HdmiInput::Initialize(PluginHost::IShell * /* service */)
	{
  	    HdmiInput::_instance = this;
	    InitializeIARM();

	    return (string());
	}

        void setResponseArray(JsonObject& response, const char* key, const vector<string>& items)
        {
            JsonArray arr;
            for(auto& i : items) arr.Add(JsonValue(i));

            response[key] = arr;

            string json;
            response.ToString(json);
        }

        void HdmiInput::Deinitialize(PluginHost::IShell* /* service */)
        {
            HdmiInput::_instance = nullptr;

            DeinitializeIARM();
        }

        void HdmiInput::InitializeIARM()
        {
            if (Utils::IARM::init())
            {
                IARM_Result_t res;
                IARM_CHECK( IARM_Bus_RegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_HOTPLUG, dsHdmiEventHandler) );
		IARM_CHECK( IARM_Bus_RegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_SIGNAL_STATUS, dsHdmiSignalStatusEventHandler) );
		IARM_CHECK( IARM_Bus_RegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_STATUS, dsHdmiStatusEventHandler) );
		IARM_CHECK( IARM_Bus_RegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_VIDEO_MODE_UPDATE, dsHdmiVideoModeEventHandler) );
		IARM_CHECK( IARM_Bus_RegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_ALLM_STATUS, dsHdmiGameFeatureStatusEventHandler) );
            }
        }

        void HdmiInput::DeinitializeIARM()
        {
            if (Utils::IARM::isConnected())
            {
                IARM_Result_t res;
                IARM_CHECK( IARM_Bus_UnRegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_HOTPLUG) );
		IARM_CHECK( IARM_Bus_UnRegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_SIGNAL_STATUS) );
		IARM_CHECK( IARM_Bus_UnRegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_STATUS) );
		IARM_CHECK( IARM_Bus_UnRegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_VIDEO_MODE_UPDATE) );
		IARM_CHECK( IARM_Bus_UnRegisterEventHandler(IARM_BUS_DSMGR_NAME,IARM_BUS_DSMGR_EVENT_HDMI_IN_ALLM_STATUS) );
            }
        }

        uint32_t HdmiInput::startHdmiInput(const JsonObject& parameters, JsonObject& response)
        {
			PLUGIN_PROFILE_START();
            LOGINFOMETHOD();
            returnIfParamNotFound(parameters, "portId");

            string sPortId = parameters["portId"].String();
            int portId = 0;
            try {
                portId = stoi(sPortId);
            }catch (const std::exception& err) {
		    LOGWARN("sPortId invalid paramater: %s ", sPortId.c_str());
			PLUGIN_PROFILE_END();
		    returnResponse(false);
            }
            bool success = true;
            try
            {
                device::HdmiInput::getInstance().selectPort(portId);
            }
            catch (const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION1(sPortId);
                success = false;
            }
			PLUGIN_PROFILE_END();
            returnResponse(success);

        }

        uint32_t HdmiInput::stopHdmiInput(const JsonObject& parameters, JsonObject& response)
        {
			PLUGIN_PROFILE_START();
            LOGINFOMETHOD();

            bool success = true;
            try
            {
                device::HdmiInput::getInstance().selectPort(-1);
            }
            catch (const device::Exception& err)
            {
                LOGWARN("HdmiInputService::stopHdmiInput Failed");
                success = false;
            }
			PLUGIN_PROFILE_END();
            returnResponse(success);

        }

        uint32_t HdmiInput::setVideoRectangleWrapper(const JsonObject& parameters, JsonObject& response)
        {
			PLUGIN_PROFILE_START();
            LOGINFOMETHOD();

            bool result = true;
            if (!parameters.HasLabel("x") && !parameters.HasLabel("y"))
            {
                result = false;
                response["message"] = "please specif coordinates (x,y)";
            }

            if (!parameters.HasLabel("w") && !parameters.HasLabel("h"))
            {
                result = false;
                response["message"] = "please specify window width and height (w,h)";
            }

            if (result)
            {
                int x = 0;
                int y = 0;
                int w = 0;
                int h = 0;

                try
		{
		       if (parameters.HasLabel("x"))
                       {
                           x = std::stoi(parameters["x"].String());
                       }
                       if (parameters.HasLabel("y"))
                       {
                           y = std::stoi(parameters["y"].String());
                       }
                       if (parameters.HasLabel("w"))
                       {
                           w = std::stoi(parameters["w"].String());
                       }
                       if (parameters.HasLabel("h"))
                       {
                           h = std::stoi(parameters["h"].String());
                       }
		}
                catch (const std::exception& err) {
		    LOGWARN("Invalid paramater X: %s,Y: %s, W: %s, H:%s ", parameters["x"].String().c_str(),parameters["y"].String().c_str(),parameters["w"].String().c_str(),parameters["h"].String().c_str());
			PLUGIN_PROFILE_END();
		    returnResponse(false);
                }

                result = setVideoRectangle(x, y, w, h);
                if (false == result) {
                  LOGWARN("HdmiInputService::setVideoRectangle Failed");
                  response["message"] = "failed to set scale";
                }
            }

			PLUGIN_PROFILE_END();
            returnResponse(result);

        }

        bool HdmiInput::setVideoRectangle(int x, int y, int width, int height)
        {
			PLUGIN_PROFILE_START();
            bool ret = true;

            try
            {
                device::HdmiInput::getInstance().scaleVideo(x, y, width, height);
            }
            catch (const device::Exception& err)
            {
                ret = false;
            }
			PLUGIN_PROFILE_END();

            return ret;
        }

        uint32_t HdmiInput::getHDMIInputDevicesWrapper(const JsonObject& parameters, JsonObject& response)
        {
			PLUGIN_PROFILE_START();
            LOGINFOMETHOD();

            response["devices"] = getHDMIInputDevices();

			PLUGIN_PROFILE_END();
            returnResponse(true);
        }

        uint32_t HdmiInput::writeEDIDWrapper(const JsonObject& parameters, JsonObject& response)
        {
			PLUGIN_PROFILE_START();
            LOGINFOMETHOD();

            int deviceId;
            std::string message;

            if (parameters.HasLabel("deviceId") && parameters.HasLabel("message"))
            {
                getNumberParameter("deviceId", deviceId);
                message = parameters["message"].String();
            }
            else
            {
                LOGWARN("Required parameters are not passed");
				PLUGIN_PROFILE_END();
                returnResponse(false);
            }


            writeEDID(deviceId, message);
			PLUGIN_PROFILE_END();
            returnResponse(true);

        }

        uint32_t HdmiInput::readEDIDWrapper(const JsonObject& parameters, JsonObject& response)
        {
			PLUGIN_PROFILE_START();
            LOGINFOMETHOD();

            string sPortId = parameters.HasLabel("deviceId") ? parameters["deviceId"].String() : "0";;
            int portId = 0;
            try {
                portId = stoi(sPortId);
            }catch (const std::exception& err) {
		    LOGWARN("sPortId invalid paramater: %s ", sPortId.c_str());
			PLUGIN_PROFILE_END();
		    returnResponse(false);
            }

            string edid = readEDID (portId);
            response["EDID"] = edid;
            if (edid.empty()) {
				PLUGIN_PROFILE_END();
                returnResponse(false);
            }
            else {
				PLUGIN_PROFILE_END();
                returnResponse(true);
            }
        }

        JsonArray HdmiInput::getHDMIInputDevices()
        {
			PLUGIN_PROFILE_START();
            JsonArray list;
            try
            {
                int num = device::HdmiInput::getInstance().getNumberOfInputs();
                if (num > 0) {
                    int i = 0;
                    for (i = 0; i < num; i++) {
                        //Input ID is aleays 0-indexed, continuous number starting 0
                        JsonObject hash;
                        hash["id"] = i;
                        std::stringstream locator;
                        locator << "hdmiin://localhost/deviceid/" << i;
                        hash["locator"] = locator.str();
                        hash["connected"] = device::HdmiInput::getInstance().isPortConnected(i) ? "true" : "false";
                        LOGWARN("HdmiInputService::getHDMIInputDevices id %d, locator=[%s], connected=[%s]", i, hash["locator"].String().c_str(), hash["connected"].String().c_str());
                        list.Add(hash);
                    }
                }
            }
            catch (const std::exception& e)  {
                LOGWARN("HdmiInputService::getHDMIInputDevices Failed");
            }
			PLUGIN_PROFILE_END();

            return list;
        }

        void HdmiInput::writeEDID(int deviceId, std::string message)
        {

        }

        std::string HdmiInput::readEDID(int iPort)
        {
			PLUGIN_PROFILE_START();
            vector<uint8_t> edidVec({'u','n','k','n','o','w','n' });
            string edidbase64 = "";
            try
            {
                vector<uint8_t> edidVec2;
                device::HdmiInput::getInstance().getEDIDBytesInfo (iPort, edidVec2);
                edidVec = edidVec2;//edidVec must be "unknown" unless we successfully get to this line

                //convert to base64
                uint16_t size = min(edidVec.size(), (size_t)numeric_limits<uint16_t>::max());

                LOGWARN("HdmiInput::readEDID size:%u edidVec.size:%d", size, (int)edidVec.size());

                if(edidVec.size() > (size_t)numeric_limits<uint16_t>::max()) {
                    LOGERR("Size too large to use ToString base64 wpe api");
					PLUGIN_PROFILE_END();
                    return edidbase64;
                }

                Core::ToString((uint8_t*)&edidVec[0], size, true, edidbase64);

            }
            catch (const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION1(std::to_string(iPort));
            }
			PLUGIN_PROFILE_END();
            return edidbase64;
        }

        /**
         * @brief This function is used to translate HDMI input hotplug to 
         * deviceChanged event.
         *
         * @param[in] input Number of input port integer.
         * @param[in] connection status of input port integer.
         */
        void HdmiInput::hdmiInputHotplug( int input , int connect)
        {
			PLUGIN_PROFILE_START();
            LOGWARN("hdmiInputHotplug [%d, %d]", input, connect);

            JsonObject params;
            params["devices"] = getHDMIInputDevices();
			PLUGIN_PROFILE_END();
            sendNotify(HDMIINPUT_EVENT_ON_DEVICES_CHANGED, params);
        }

        /**
         * @brief This function is used to translate HDMI input signal change to
         * signalChanged event.
         *
         * @param[in] port HDMI In port id.
         * @param[in] signalStatus signal status of HDMI In port.
         */
        void HdmiInput::hdmiInputSignalChange( int port , int signalStatus)
        {
			PLUGIN_PROFILE_START();
            LOGWARN("hdmiInputSignalStatus [%d, %d]", port, signalStatus);

            JsonObject params;
            params["id"] = port;
            std::stringstream locator;
            locator << "hdmiin://localhost/deviceid/" << port;
            params["locator"] = locator.str();

	    switch (signalStatus) {
		    case dsHDMI_IN_SIGNAL_STATUS_NOSIGNAL:
			    params["signalStatus"] = "noSignal";
			    break;

	            case dsHDMI_IN_SIGNAL_STATUS_UNSTABLE:
			    params["signalStatus"] = "unstableSignal";
			    break;

                    case dsHDMI_IN_SIGNAL_STATUS_NOTSUPPORTED:
                            params["signalStatus"] = "notSupportedSignal";
                            break;

                    case dsHDMI_IN_SIGNAL_STATUS_STABLE:
                            params["signalStatus"] = "stableSignal";
                            break;

	            default:
                            params["signalStatus"] = "none";
                            break;
            }
			PLUGIN_PROFILE_END();

            sendNotify(HDMIINPUT_EVENT_ON_SIGNAL_CHANGED, params);
        }

        /**
         * @brief This function is used to translate HDMI input status change to
         * inputStatusChanged event.
         *
         * @param[in] port HDMI In port id.
         * @param[bool] isPresented HDMI In presentation started/stopped.
         */
        void HdmiInput::hdmiInputStatusChange( int port , bool isPresented)
        {
			PLUGIN_PROFILE_START();
            LOGWARN("hdmiInputStatus [%d, %d]", port, isPresented);

            JsonObject params;
            params["id"] = port;
            std::stringstream locator;
            locator << "hdmiin://localhost/deviceid/" << port;
            params["locator"] = locator.str();

            if(isPresented) {
                params["status"] = "started";
            }
	    else {
                params["status"] = "stopped";
            }
			PLUGIN_PROFILE_END();

            sendNotify(HDMIINPUT_EVENT_ON_STATUS_CHANGED, params);
        }

        /**
         * @brief This function is used to translate HDMI input video mode change to
         * videoStreamInfoUpdate event.
         *
         * @param[in] port HDMI In port id.
         * @param[dsVideoPortResolution_t] video resolution data
         */
        void HdmiInput::hdmiInputVideoModeUpdate( int port , dsVideoPortResolution_t resolution)
        {
			PLUGIN_PROFILE_START();
            LOGWARN("hdmiInputVideoModeUpdate [%d]", port);

            JsonObject params;
            params["id"] = port;
            std::stringstream locator;
            locator << "hdmiin://localhost/deviceid/" << port;
            params["locator"] = locator.str();

	    switch(resolution.pixelResolution) {
		    case dsVIDEO_PIXELRES_720x480:
			params["width"] = 720;
			params["height"] = 480;
			break;

		    case dsVIDEO_PIXELRES_720x576:
                        params["width"] = 720;
                        params["height"] = 576;
                        break;

                    case dsVIDEO_PIXELRES_1280x720:
                        params["width"] = 1280;
                        params["height"] = 720;
                        break;

                    case dsVIDEO_PIXELRES_1920x1080:
                        params["width"] = 1920;
                        params["height"] = 1080;
                        break;

                    case dsVIDEO_PIXELRES_3840x2160:
                        params["width"] = 3840;
                        params["height"] = 2160;
                        break;

                    case dsVIDEO_PIXELRES_4096x2160:
                        params["width"] = 4096;
                        params["height"] = 2160;
                        break;

		    default:
                        params["width"] = 1920;
                        params["height"] = 1080;
                        break;
	    }

	    params["progressive"] = (!resolution.interlaced);

            switch(resolution.frameRate) {
                    case dsVIDEO_FRAMERATE_24:
                        params["frameRateN"] = 24000;
                        params["frameRateD"] = 1000;
                        break;

                    case dsVIDEO_FRAMERATE_25:
                        params["frameRateN"] = 25000;
                        params["frameRateD"] = 1000;
                        break;

                    case dsVIDEO_FRAMERATE_30:
                        params["frameRateN"] = 30000;
                        params["frameRateD"] = 1000;
                        break;

                    case dsVIDEO_FRAMERATE_50:
                        params["frameRateN"] = 50000;
                        params["frameRateD"] = 1000;
                        break;

                    case dsVIDEO_FRAMERATE_60:
                        params["frameRateN"] = 60000;
                        params["frameRateD"] = 1000;
                        break;

                    case dsVIDEO_FRAMERATE_23dot98:
                        params["frameRateN"] = 24000;
                        params["frameRateD"] = 1001;
                        break;

                    case dsVIDEO_FRAMERATE_29dot97:
                        params["frameRateN"] = 30000;
                        params["frameRateD"] = 1001;
                        break;

                    case dsVIDEO_FRAMERATE_59dot94:
                        params["frameRateN"] = 60000;
                        params["frameRateD"] = 1001;
                        break;

                    default:
                        params["frameRateN"] = 60000;
                        params["frameRateD"] = 1000;
                        break;
            }

			PLUGIN_PROFILE_END();
            sendNotify(HDMIINPUT_EVENT_ON_VIDEO_MODE_UPDATED, params);
        }

        void HdmiInput::dsHdmiEventHandler(const char *owner, IARM_EventId_t eventId, void *data, size_t len)
        {
			PLUGIN_PROFILE_START();
            if(!HdmiInput::_instance)
			{
				PLUGIN_PROFILE_END();
                return;
			}

            if (IARM_BUS_DSMGR_EVENT_HDMI_IN_HOTPLUG == eventId)
            {
                IARM_Bus_DSMgr_EventData_t *eventData = (IARM_Bus_DSMgr_EventData_t *)data;
                int hdmiin_hotplug_port = eventData->data.hdmi_in_connect.port;
                int hdmiin_hotplug_conn = eventData->data.hdmi_in_connect.isPortConnected;
                LOGWARN("Received IARM_BUS_DSMGR_EVENT_HDMI_IN_HOTPLUG  event data:%d", hdmiin_hotplug_port);

                HdmiInput::_instance->hdmiInputHotplug(hdmiin_hotplug_port, hdmiin_hotplug_conn ? HDMI_HOT_PLUG_EVENT_CONNECTED : HDMI_HOT_PLUG_EVENT_DISCONNECTED);
            }
			PLUGIN_PROFILE_END();
        }

        void HdmiInput::dsHdmiSignalStatusEventHandler(const char *owner, IARM_EventId_t eventId, void *data, size_t len)
        {
			PLUGIN_PROFILE_START();
            if(!HdmiInput::_instance)
			{
				PLUGIN_PROFILE_END();
                return;
			}

            if (IARM_BUS_DSMGR_EVENT_HDMI_IN_SIGNAL_STATUS == eventId)
            {
                IARM_Bus_DSMgr_EventData_t *eventData = (IARM_Bus_DSMgr_EventData_t *)data;
                int hdmi_in_port = eventData->data.hdmi_in_sig_status.port;
                int hdmi_in_signal_status = eventData->data.hdmi_in_sig_status.status;
                LOGWARN("Received IARM_BUS_DSMGR_EVENT_HDMI_IN_SIGNAL_STATUS  event  port: %d, signal status: %d", hdmi_in_port,hdmi_in_signal_status);

                HdmiInput::_instance->hdmiInputSignalChange(hdmi_in_port, hdmi_in_signal_status);

            }
			PLUGIN_PROFILE_END();
        }

        void HdmiInput::dsHdmiStatusEventHandler(const char *owner, IARM_EventId_t eventId, void *data, size_t len)
        {
			PLUGIN_PROFILE_START();
            if(!HdmiInput::_instance)
			{
				PLUGIN_PROFILE_END();
                return;
			}

            if (IARM_BUS_DSMGR_EVENT_HDMI_IN_STATUS == eventId)
            {
                IARM_Bus_DSMgr_EventData_t *eventData = (IARM_Bus_DSMgr_EventData_t *)data;
                int hdmi_in_port = eventData->data.hdmi_in_status.port;
                bool hdmi_in_status = eventData->data.hdmi_in_status.isPresented;
                LOGWARN("Received IARM_BUS_DSMGR_EVENT_HDMI_IN_STATUS  event  port: %d, started: %d", hdmi_in_port,hdmi_in_status);

                HdmiInput::_instance->hdmiInputStatusChange(hdmi_in_port, hdmi_in_status);

            }
			PLUGIN_PROFILE_END();
        }

        void HdmiInput::dsHdmiVideoModeEventHandler(const char *owner, IARM_EventId_t eventId, void *data, size_t len)
        {
			PLUGIN_PROFILE_START();
            if(!HdmiInput::_instance)
			{
				PLUGIN_PROFILE_END();
                return;
			}

            if (IARM_BUS_DSMGR_EVENT_HDMI_IN_VIDEO_MODE_UPDATE == eventId)
            {
                IARM_Bus_DSMgr_EventData_t *eventData = (IARM_Bus_DSMgr_EventData_t *)data;
                int hdmi_in_port = eventData->data.hdmi_in_video_mode.port;
		dsVideoPortResolution_t resolution;
		resolution.pixelResolution =  eventData->data.hdmi_in_video_mode.resolution.pixelResolution;
		resolution.interlaced =  eventData->data.hdmi_in_video_mode.resolution.interlaced;
		resolution.frameRate =  eventData->data.hdmi_in_video_mode.resolution.frameRate;
                LOGWARN("Received IARM_BUS_DSMGR_EVENT_HDMI_IN_VIDEO_MODE_UPDATE  event  port: %d, pixelResolution: %d, interlaced : %d, frameRate: %d \n", hdmi_in_port,resolution.pixelResolution, resolution.interlaced, resolution.frameRate);

                HdmiInput::_instance->hdmiInputVideoModeUpdate(hdmi_in_port, resolution);

            }
			PLUGIN_PROFILE_END();
        }

        void HdmiInput::dsHdmiGameFeatureStatusEventHandler(const char *owner, IARM_EventId_t eventId, void *data, size_t len)
        {
			PLUGIN_PROFILE_START();
            if(!HdmiInput::_instance)
			{
				PLUGIN_PROFILE_END();
                return;
			}

            if (IARM_BUS_DSMGR_EVENT_HDMI_IN_ALLM_STATUS == eventId)
            {
                IARM_Bus_DSMgr_EventData_t *eventData = (IARM_Bus_DSMgr_EventData_t *)data;
                int hdmi_in_port = eventData->data.hdmi_in_allm_mode.port;
                bool allm_mode = eventData->data.hdmi_in_allm_mode.allm_mode;
                LOGWARN("Received IARM_BUS_DSMGR_EVENT_HDMI_IN_ALLM_STATUS  event  port: %d, ALLM Mode: %d", hdmi_in_port,allm_mode);

                HdmiInput::_instance->hdmiInputALLMChange(hdmi_in_port, allm_mode);
            }
			PLUGIN_PROFILE_END();
        }

        void HdmiInput::hdmiInputALLMChange( int port , bool allm_mode)
        {
			PLUGIN_PROFILE_START();
            JsonObject params;
            params["id"] = port;
            params["gameFeature"] = "ALLM";
            params["mode"] = allm_mode;

			PLUGIN_PROFILE_END();
            sendNotify(HDMIINPUT_EVENT_ON_GAME_FEATURE_STATUS_CHANGED, params);
        }

        uint32_t HdmiInput::getSupportedGameFeatures(const JsonObject& parameters, JsonObject& response)
        {
			PLUGIN_PROFILE_START();
            LOGINFOMETHOD();
            vector<string> supportedFeatures;
            try
            {
                device::HdmiInput::getInstance().getSupportedGameFeatures (supportedFeatures);
                for (size_t i = 0; i < supportedFeatures.size(); i++)
                {
                    LOGINFO("Supported Game Feature [%d]:  %s\n",(int)i,supportedFeatures.at(i).c_str());
                }
            }
            catch (const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION0();
            }

            if (supportedFeatures.empty()) {
				PLUGIN_PROFILE_END();
                returnResponse(false);
            }
            else {
                setResponseArray(response, "supportedGameFeatures", supportedFeatures);
				PLUGIN_PROFILE_END();
                returnResponse(true);
            }
        }

        uint32_t HdmiInput::getHdmiGameFeatureStatusWrapper(const JsonObject& parameters, JsonObject& response)
        {
			PLUGIN_PROFILE_START();
            string sPortId = parameters["portId"].String();
            string sGameFeature = parameters["gameFeature"].String();
            int portId = 0;

            LOGINFOMETHOD();
            returnIfParamNotFound(parameters, "portId");
            returnIfParamNotFound(parameters, "gameFeature");
            try {
                portId = stoi(sPortId);
            }catch (const std::exception& err) {
		    LOGWARN("sPortId invalid paramater: %s ", sPortId.c_str());
			PLUGIN_PROFILE_END();
		    returnResponse(false);
            }

	    if (strcmp (sGameFeature.c_str(), "ALLM") == 0)
            {
                bool allm = getHdmiALLMStatus(portId);
                LOGWARN("HdmiInput::getHdmiGameFeatureStatusWrapper ALLM MODE:%d", allm);
                response["mode"] = allm;
            }
	    else
	    {
		LOGWARN("HdmiInput::getHdmiGameFeatureStatusWrapper Mode is not supported. Supported mode: ALLM");
		response["message"] = "Mode is not supported. Supported mode: ALLM";
		PLUGIN_PROFILE_END();
		returnResponse(false);
	    }
		PLUGIN_PROFILE_END();
            returnResponse(true);
        }

        bool HdmiInput::getHdmiALLMStatus(int iPort)
        {
			PLUGIN_PROFILE_START();
            bool allm = false;

            try
            {
                device::HdmiInput::getInstance().getHdmiALLMStatus (iPort, &allm);
                LOGWARN("HdmiInput::getHdmiALLMStatus ALLM MODE: %d", allm);
            }
            catch (const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION1(std::to_string(iPort));
            }
			PLUGIN_PROFILE_END();
            return allm;
        }

        uint32_t HdmiInput::getRawHDMISPDWrapper(const JsonObject& parameters, JsonObject& response)
        {
			PLUGIN_PROFILE_START();
            LOGINFOMETHOD();
           returnIfParamNotFound(parameters, "portId");

           string sPortId = parameters["portId"].String();
            int portId = 0;
            try {
                portId = stoi(sPortId);
            }catch (const std::exception& err) {
		    LOGWARN("sPortId invalid paramater: %s ", sPortId.c_str());
			PLUGIN_PROFILE_END();
		    returnResponse(false);
            }

            string spdInfo = getRawHDMISPD (portId);
            response["HDMISPD"] = spdInfo;
            if (spdInfo.empty()) {
				PLUGIN_PROFILE_END();
                returnResponse(false);
            }
            else {
				PLUGIN_PROFILE_END();
                returnResponse(true);
            }
        }

        uint32_t HdmiInput::getHDMISPDWrapper(const JsonObject& parameters, JsonObject& response)
        {
			PLUGIN_PROFILE_START();
            LOGINFOMETHOD();
           returnIfParamNotFound(parameters, "portId");

           string sPortId = parameters["portId"].String();
            int portId = 0;
            try {
                portId = stoi(sPortId);
            }catch (const std::exception& err) {
		    LOGWARN("sPortId invalid paramater: %s ", sPortId.c_str());
			PLUGIN_PROFILE_END();
		    returnResponse(false);
            }

            string spdInfo = getHDMISPD (portId);
            response["HDMISPD"] = spdInfo;
            if (spdInfo.empty()) {
				PLUGIN_PROFILE_END();
                returnResponse(false);
            }
            else {
				PLUGIN_PROFILE_END();
                returnResponse(true);
            }
        }

        std::string HdmiInput::getRawHDMISPD(int iPort)
        {
			PLUGIN_PROFILE_START();
                LOGINFO("HdmiInput::getHDMISPDInfo");
                vector<uint8_t> spdVect({'u','n','k','n','o','w','n' });
                std::string spdbase64 = "";
            try
            {
                LOGWARN("HdmiInput::getHDMISPDInfo");
                vector<uint8_t> spdVect2;
                device::HdmiInput::getInstance().getHDMISPDInfo(iPort, spdVect2);
                spdVect = spdVect2;//edidVec must be "unknown" unless we successfully get to this line

                //convert to base64
                uint16_t size = min(spdVect.size(), (size_t)numeric_limits<uint16_t>::max());

                LOGWARN("HdmiInput::getHDMISPD size:%u spdVec.size:%d", size, (int)spdVect.size());

                if(spdVect.size() > (size_t)numeric_limits<uint16_t>::max()) {
                    LOGERR("Size too large to use ToString base64 wpe api");
					PLUGIN_PROFILE_END();
                    return spdbase64;
                }

                LOGINFO("------------getHDMISPD: ");
                for (unsigned int itr =0; itr < spdVect.size(); itr++) {
                  LOGINFO("%02X ", spdVect[itr]);
                }
                Core::ToString((uint8_t*)&spdVect[0], size, false, spdbase64);

            }
            catch (const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION1(std::to_string(iPort));
            }
			PLUGIN_PROFILE_END();
            return spdbase64;
        }

        std::string HdmiInput::getHDMISPD(int iPort)
        {
			PLUGIN_PROFILE_START();
                LOGINFO("HdmiInput::getHDMISPDInfo");
                vector<uint8_t> spdVect({'u','n','k','n','o','w','n' });
                std::string spdbase64 = "";
            try
            {
                LOGWARN("HdmiInput::getHDMISPDInfo");
                vector<uint8_t> spdVect2;
                device::HdmiInput::getInstance().getHDMISPDInfo(iPort, spdVect2);
                spdVect = spdVect2;//edidVec must be "unknown" unless we successfully get to this line

                //convert to base64
                uint16_t size = min(spdVect.size(), (size_t)numeric_limits<uint16_t>::max());

                LOGWARN("HdmiInput::getHDMISPD size:%u spdVec.size:%d", size, (int)spdVect.size());

                if(spdVect.size() > (size_t)numeric_limits<uint16_t>::max()) {
                    LOGERR("Size too large to use ToString base64 wpe api");
					PLUGIN_PROFILE_END();
                    return spdbase64;
                }

                LOGINFO("------------getHDMISPD: ");
                for (unsigned int itr =0; itr < spdVect.size(); itr++) {
                  LOGINFO("%02X ", spdVect[itr]);
                }
               if (spdVect.size() > 0) {
                struct dsSpd_infoframe_st pre;
                memcpy(&pre,spdVect.data(),sizeof(struct dsSpd_infoframe_st));

              char str[200] = {0};
               sprintf(str, "Packet Type:%02X,Version:%u,Length:%u,vendor name:%s,product des:%s,source info:%02X"
,pre.pkttype,pre.version,pre.length,pre.vendor_name,pre.product_des,pre.source_info);
              spdbase64 = str;
               }
            }
            catch (const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION1(std::to_string(iPort));
            }
			PLUGIN_PROFILE_END();
            return spdbase64;
        }

        uint32_t HdmiInput::setEdidVersionWrapper(const JsonObject& parameters, JsonObject& response)
        {
			PLUGIN_PROFILE_START();
            int portId = 0;

            LOGINFOMETHOD();
            returnIfParamNotFound(parameters, "portId");
            returnIfParamNotFound(parameters, "edidVersion");
            string sPortId = parameters["portId"].String();
            string sVersion = parameters["edidVersion"].String();
            try {
                portId = stoi(sPortId);
            }catch (const std::exception& err) {
                    LOGWARN("sPortId invalid paramater: %s ", sPortId.c_str());
					PLUGIN_PROFILE_END();
		    returnResponse(false);
            }

            int edidVer = -1;
            if (strcmp (sVersion.c_str(), "HDMI1.4") == 0) {
                edidVer = HDMI_EDID_VER_14;
            }
            else if (strcmp (sVersion.c_str(), "HDMI2.0") == 0) {
                edidVer = HDMI_EDID_VER_20;
            }

            if (edidVer < 0) {
				PLUGIN_PROFILE_END();
                returnResponse(false);
            }
            bool result = setEdidVersion (portId, edidVer);
            if (result == false) {
				PLUGIN_PROFILE_END();
                returnResponse(false);
            }
            else {
				PLUGIN_PROFILE_END();
                returnResponse(true);
            }
			PLUGIN_PROFILE_END();
        }

        int HdmiInput::setEdidVersion(int iPort, int iEdidVer)
        {
			PLUGIN_PROFILE_START();
            bool ret = true;
            try
            {
                device::HdmiInput::getInstance().setEdidVersion (iPort, iEdidVer);
                LOGWARN("HdmiInput::setEdidVersion EDID Version:%d", iEdidVer);
            }
            catch (const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION1(std::to_string(iPort));
                ret = false;
            }
			PLUGIN_PROFILE_END();
            return ret;
        }

        uint32_t HdmiInput::getEdidVersionWrapper(const JsonObject& parameters, JsonObject& response)
        {
			PLUGIN_PROFILE_START();
            string sPortId = parameters["portId"].String();
            int portId = 0;

            LOGINFOMETHOD();
            returnIfParamNotFound(parameters, "portId");
            try {
                portId = stoi(sPortId);
            }catch (const std::exception& err) {
		    LOGWARN("sPortId invalid paramater: %s ", sPortId.c_str());
			PLUGIN_PROFILE_END();
		    returnResponse(false);
            }

            int edidVer = getEdidVersion (portId);
            switch (edidVer)
            {
                case HDMI_EDID_VER_14:
                    response["edidVersion"] = "HDMI1.4";
                    break;
                case HDMI_EDID_VER_20:
                    response["edidVersion"] = "HDMI2.0";
                    break;
            }

            if (edidVer < 0) {
				PLUGIN_PROFILE_END();
                returnResponse(false);
            }
            else {
				PLUGIN_PROFILE_END();
                returnResponse(true);
            }
        }

        int HdmiInput::getEdidVersion(int iPort)
        {
			PLUGIN_PROFILE_START();
            int edidVersion = -1;

            try
            {
                device::HdmiInput::getInstance().getEdidVersion (iPort, &edidVersion);
                LOGWARN("HdmiInput::getEdidVersion EDID Version:%d", edidVersion);
            }
            catch (const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION1(std::to_string(iPort));
            }
			PLUGIN_PROFILE_END();
            return edidVersion;
        }

    } // namespace Plugin
} // namespace WPEFramework
