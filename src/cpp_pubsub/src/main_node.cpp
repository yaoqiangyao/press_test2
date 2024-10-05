#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include <chrono>
#include <sstream>
#include <vector>
#include <mutex>

class MainNode : public rclcpp::Node
{
public:
  MainNode(int n)
  : Node("main_node"),
    sequences_(n, 0),
    min_delay_(n, INT64_MAX),
    max_delay_(n, 0),
    send_count_(n, 0),
    receive_count_(n, 0)
  {
    // 创建 n 个发布者和订阅者
    for (int i = 1; i <= n; ++i) {
      auto publisher = this->create_publisher<std_msgs::msg::String>(
        "hello_" + std::to_string(i) + "_main", 10);
      publishers_.push_back(publisher);

      auto subscriber = this->create_subscription<std_msgs::msg::String>(
        "hello_" + std::to_string(i) + "_relay", 10,
        [this, i](const std_msgs::msg::String::SharedPtr msg) {
          this->message_callback(msg, i);
        });
      subscribers_.push_back(subscriber);

      // 为每个主题创建独立的定时器
      auto timer = this->create_wall_timer(
        std::chrono::milliseconds(500),
        [this, i]() {publish_message(i);});
      timers_.push_back(timer);
    }

    // 创建汇总信息定时器
    summary_timer_ = this->create_wall_timer(
      std::chrono::seconds(1), [this]() {print_summary();});
  }

  void publish_message(int topic_number)
  {
    auto now = std::chrono::steady_clock::now();
    auto timestamp =
      std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

    std::ostringstream message_stream;
    message_stream << timestamp << "," << sequences_[topic_number - 1]++;

    std_msgs::msg::String message;
    message.data = message_stream.str();
    publishers_[topic_number - 1]->publish(message);

    // 增加发送计数
    send_count_[topic_number - 1]++;
  }

  void message_callback(const std_msgs::msg::String::SharedPtr msg, int topic_number)
  {
    auto now = std::chrono::steady_clock::now();
    auto current_time =
      std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

    std::string received_data = msg->data;
    std::size_t comma_pos = received_data.find(',');
    if (comma_pos != std::string::npos) {
      auto received_timestamp = std::stoll(received_data.substr(0, comma_pos));
      auto delay = current_time - received_timestamp;

      // 更新延迟统计
      {
        std::lock_guard<std::mutex> lock(mutex_);
        receive_count_[topic_number - 1]++;
        if (delay < min_delay_[topic_number - 1]) {min_delay_[topic_number - 1] = delay;}
        if (delay > max_delay_[topic_number - 1]) {max_delay_[topic_number - 1] = delay;}
      }
    }
  }

  void print_summary()
  {
    // 打印第一个队列的汇总信息
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (publishers_.size() > 0) {
        RCLCPP_INFO(
          this->get_logger(),
          "Summary for topic1: Send Count: %d, Receive Count: %d, Min Delay: %ld µs, Max Delay: %ld µs",
          send_count_[0], receive_count_[0], min_delay_[0], max_delay_[0]);

        // 重置第一个队列的统计信息
        min_delay_[0] = INT64_MAX;
        max_delay_[0] = 0;
      }
    }
  }

private:
  std::vector<rclcpp::Publisher<std_msgs::msg::String>::SharedPtr> publishers_;
  std::vector<rclcpp::Subscription<std_msgs::msg::String>::SharedPtr> subscribers_;
  std::vector<rclcpp::TimerBase::SharedPtr> timers_;   // 存储每个主题的定时器
  rclcpp::TimerBase::SharedPtr summary_timer_;   // 汇总信息定时器

  std::vector<int64_t> sequences_;   // 每个主题的独立序列号
  std::vector<int64_t> min_delay_;   // 每个主题的最小延迟
  std::vector<int64_t> max_delay_;   // 每个主题的最大延迟
  std::vector<int> send_count_;   // 每个主题的发送数量
  std::vector<int> receive_count_;   // 每个主题的接收数量

  std::mutex mutex_;   // 保护共享数据的互斥锁
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  // 获取输入参数
  int topicNum = 1;     // 默认值
  if (argc > 1) {
    topicNum = std::stoi(argv[1]);         // 从命令行参数获取topicNum
  }

  auto node = std::make_shared<MainNode>(topicNum);

  // 创建多线程执行器
  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(node);

  // 使用多线程处理节点
  executor.spin();

  rclcpp::shutdown();
  return 0;
}
