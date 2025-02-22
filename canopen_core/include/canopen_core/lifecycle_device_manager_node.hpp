//    Copyright 2022 Harshavadan Deshpande
//                   Christoph Hellmann Santos
//
//    Licensed under the Apache License, Version 2.0 (the "License");
//    you may not use this file except in compliance with the License.
//    You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//    See the License for the specific language governing permissions and
//    limitations under the License.
#ifndef LIFECYCLE_DEVICE_MANAGER_NODE_HPP
#define LIFECYCLE_DEVICE_MANAGER_NODE_HPP


#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "lifecycle_msgs/msg/state.hpp"
#include "lifecycle_msgs/msg/transition.hpp"
#include "lifecycle_msgs/srv/change_state.hpp"
#include "lifecycle_msgs/srv/get_state.hpp"
#include "std_srvs/srv/trigger.hpp"

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"

#include "canopen_core/configuration_manager.hpp"
#include "canopen_interfaces/srv/co_node.hpp"

using namespace std::chrono_literals;
/*
Start-up sequence
--------------------------------------------------------------------
master | unconfigured | x |
       | inactive     |   | x |
       | active       |       | x | x | x | x | x |...
--------------------------------------------------------------------
drv1   | unconfigured | x | x | x |
       | inactive     |           | x |
       | active       |               | x | x | x |...
--------------------------------------------------------------------
driv2  | unconfigured | x | x | x | x | x |
       | inactive     |                   | x |
       | active       |                       | x |...
--------------------------------------------------------------------
*/
namespace ros2_canopen
{
    class LifecycleDeviceManagerNode : public rclcpp_lifecycle::LifecycleNode
    {
    public:
        LifecycleDeviceManagerNode(const rclcpp::NodeOptions &node_options)
            : rclcpp_lifecycle::LifecycleNode("LifecycleDeviceManagerNode", node_options)
        {
            this->declare_parameter("container_name", "");
            cbg_clients = this->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
        }

        void init(std::shared_ptr<ros2_canopen::ConfigurationManager> config);

    protected:
        rclcpp::CallbackGroup::SharedPtr cbg_clients;
        std::shared_ptr<ros2_canopen::ConfigurationManager> config_;

        rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
        on_configure(const rclcpp_lifecycle::State &state);

        rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
        on_activate(const rclcpp_lifecycle::State &state);

        rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
        on_deactivate(const rclcpp_lifecycle::State &state);

        rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
        on_cleanup(const rclcpp_lifecycle::State &state);

        rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
        on_shutdown(const rclcpp_lifecycle::State &state);

        // stores node_id and get_state client
        std::map<uint8_t, rclcpp::Client<lifecycle_msgs::srv::GetState>::SharedPtr> drivers_get_state_clients;
        // stores node_id and change_state client
        std::map<uint8_t, rclcpp::Client<lifecycle_msgs::srv::ChangeState>::SharedPtr> drivers_change_state_clients;
        // stores device_name and node_id
        std::map<std::string, uint8_t> device_names_to_ids;

        rclcpp::Client<canopen_interfaces::srv::CONode>::SharedPtr add_driver_client_;
        rclcpp::Client<canopen_interfaces::srv::CONode>::SharedPtr remove_driver_client_;

        uint8_t master_id_;
        std::string container_name_;

    private:
        template <typename FutureT, typename WaitTimeT>
        std::future_status
        wait_for_result(
            FutureT &future,
            WaitTimeT time_to_wait)
        {
            auto end = std::chrono::steady_clock::now() + time_to_wait;
            std::chrono::milliseconds wait_period(100);
            std::future_status status = std::future_status::timeout;
            do
            {
                auto now = std::chrono::steady_clock::now();
                auto time_left = end - now;
                if (time_left <= std::chrono::seconds(0))
                {
                    break;
                }
                status = future.wait_for((time_left < wait_period) ? time_left : wait_period);
            } while (rclcpp::ok() && status != std::future_status::ready);
            return status;
        }

        unsigned int
        get_state(uint8_t node_id, std::chrono::seconds time_out = 3s);

        bool
        change_state(uint8_t node_id, uint8_t transition, std::chrono::seconds time_out = 3s);

        /**
         * @brief Brings up master and all drivers
         *
         * This funnnction brings up the CANopen master and all drivers that are defined
         * in the configuration. The order of bringing up drivers is arbitrary.
         *
         * @return true
         * @return false
         */
        bool
        bring_up_all();

        /**
         * @brief Brings down master and all drivers
         *
         * This funnnction brings up the CANopen master and all drivers that are defined
         * in the configuration. First all drivers are brought down, then the master.
         *
         * @return true
         * @return false
         */
        bool
        bring_down_all();

        /**
         * @brief Brings up master
         *
         * This function brings up the CANopen master. The master transitioned twice,
         * first the configure transition is triggered. Once the transition is successfully
         * finished, the activate transition is triggered.
         *
         * @return true
         * @return false
         */
        bool
        bring_up_master();

        /**
         * @brief Bring down master
         *
         * This function brrignsdown the CANopen master. The master transitioned twice,
         * first the deactivate transition is triggered. Once the transition is successfully
         * finished, the cleanup transition is triggered.
         *
         * @return true
         * @return false
         */
        bool
        bring_down_master();

        /**
         * @brief Brings up the drivers with specified node_id
         *
         * This function bringsup the CANopen driver for the device with the specified
         * node_id. The driver is transitioned twice,
         * first the configure transition is triggered. Once the transition is successfully
         * finished, the activate transition is triggered.
         *
         * @param device_name
         * @return true
         * @return false
         */
        bool
        bring_up_driver(std::string device_name);

        /**
         * @brief Brings down the driver with specified node_id
         *
         * This function brings down the CANopen driver for the device with the specified
         * node_id. The driver is transitioned twice,
         * first the deactivate transition is triggered. Once the transition is successfully
         * finished, the cleanup transition is triggered.
         *
         * @param device_name
         * @return true
         * @return false
         */
        bool
        bring_down_driver(std::string device_name);

        /**
         * @brief Load information from configuration
         *
         * This function loads the information about the CANopen Bus specified in
         * the configuration file and creates the necessary ROS2 services and clients.
         *
         * @return true
         * @return false
         */
        bool
        load_from_config();
    };
}

#endif