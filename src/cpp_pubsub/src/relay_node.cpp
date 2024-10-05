#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <iostream>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

using namespace std::chrono_literals;

class RelayNode : public rclcpp::Node
{
public:
  RelayNode(int n)
  : Node("relay_node")
  {
    // 打印启动日志
    RCLCPP_INFO(this->get_logger(), "启动RelayNode with topicNum: %d", n);

    for (int i = 1; i <= n; ++i) {
      std::string main_topic = "hello_" + std::to_string(i) + "_main";
      std::string relay_topic = "hello_" + std::to_string(i) + "_relay";

      auto subscriber = this->create_subscription<std_msgs::msg::String>(
        main_topic, 10,
        [this, i](const std_msgs::msg::String::SharedPtr msg) {
          this->message_callback(msg, i);
        });
      subscribers_.push_back(subscriber);

      auto publisher = this->create_publisher<std_msgs::msg::String>(
        relay_topic, 10);
      publishers_.push_back(publisher);

      // 打印订阅和发布的主题名称
      RCLCPP_INFO(this->get_logger(), "Subscribed to topic: %s", main_topic.c_str());
      RCLCPP_INFO(this->get_logger(), "Publishing to topic: %s", relay_topic.c_str());
    }
  }

  void message_callback(const std_msgs::msg::String::SharedPtr msg, int topic_number)
  {
    if (topic_number == 1) {
      RCLCPP_INFO(
        this->get_logger(), "收到消息来自主节点 topic hello_%d_main: %s", topic_number,
        msg->data.c_str());
    }
    publishers_[topic_number - 1]->publish(*msg);
  }

private:
  std::vector<rclcpp::Publisher<std_msgs::msg::String>::SharedPtr> publishers_;
  std::vector<rclcpp::Subscription<std_msgs::msg::String>::SharedPtr> subscribers_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  // 获取输入参数
  int topicNum = 1;   // 默认值
  if (argc > 1) {
    topicNum = std::stoi(argv[1]);     // 从命令行参数获取topicNum
  }

  auto node = std::make_shared<RelayNode>(topicNum);

  // 创建多线程执行器
  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(node);

  // 使用多线程处理节点
  executor.spin();

  rclcpp::shutdown();
  return 0;
}
