#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include <chrono>
#include <sstream>
#include <vector>
#include <mutex>
#include <numeric>

class MainNode : public rclcpp::Node
{
public:
  MainNode(int n, int runtime, int frequency)
  : Node("main_node"),
    sequences_(n, 0),            // 初始化每个主题的序列号为 0
    message_counts_(n, 0),        // 初始化每个主题的消息计数为 0
    received_delays_(n)           // 初始化每个主题的延迟记录
  {
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

      auto timer = this->create_wall_timer(
        std::chrono::milliseconds(1000 / frequency),
        [this, i]() {publish_message(i);});
      timers_.push_back(timer);
    }

    // 创建汇总信息定时器
    summary_timer_ = this->create_wall_timer(
      std::chrono::seconds(1), [this]() {print_summary();});

    // 创建运行结束定时器
    end_timer_ = this->create_wall_timer(
      std::chrono::seconds(runtime), [this]() {stop_publishing();});
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
    message_counts_[topic_number - 1]++;
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

      // 记录收到的消息及其延迟
      {
        std::lock_guard<std::mutex> lock(mutex_);
        received_delays_[topic_number - 1].push_back(delay);
      }
    }
  }

  void print_summary()
  {
    // 打印第一个 topic 的序列号和收到的消息总数
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!message_counts_.empty()) {
        RCLCPP_INFO(
          this->get_logger(),
          "Summary for topic1: Current Sequence: %ld, Total Received: %d",
          sequences_[0], message_counts_[0]);
      }
    }
  }

  void stop_publishing()
  {
    for (auto & timer : timers_) {
      timer->cancel();        // 停止发布定时器
    }
    summary_timer_->cancel();   // 停止汇总信息定时器

    // 检查发送和接收消息数量
    for (size_t i = 0; i < message_counts_.size(); ++i) {
      RCLCPP_WARN(
        this->get_logger(), "Mismatch in topic %ld: Sent: %ld, Received: %d",
        i + 1, sequences_[i], message_counts_[i]);
      // 延迟10秒后开始打印记录的信息
      this->create_wall_timer(
        std::chrono::seconds(10), [this, i]() {print_received_delays(i);});
    }

    rclcpp::shutdown();
  }

  void print_received_delays(int topic_number)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!received_delays_[topic_number - 1].empty()) {
      auto min_delay = *std::min_element(
        received_delays_[topic_number - 1].begin(),
        received_delays_[topic_number - 1].end());
      auto max_delay = *std::max_element(
        received_delays_[topic_number - 1].begin(),
        received_delays_[topic_number - 1].end());
      auto average_delay = std::accumulate(
        received_delays_[topic_number - 1].begin(),
        received_delays_[topic_number - 1].end(), 0.0) /
        received_delays_[topic_number - 1].size();

      RCLCPP_INFO(
        this->get_logger(),
        "Topic %d Delays - Min: %ld µs, Max: %ld µs, Average: %.2f µs",
        topic_number, min_delay, max_delay, average_delay);
    }
  }

private:
  std::vector<rclcpp::Publisher<std_msgs::msg::String>::SharedPtr> publishers_;
  std::vector<rclcpp::Subscription<std_msgs::msg::String>::SharedPtr> subscribers_;
  std::vector<rclcpp::TimerBase::SharedPtr> timers_;
  rclcpp::TimerBase::SharedPtr summary_timer_;
  rclcpp::TimerBase::SharedPtr end_timer_;

  std::vector<int64_t> sequences_;     // 每个主题的独立序列号
  std::vector<int> message_counts_;     // 每个主题的消息计数
  std::vector<std::vector<int64_t>> received_delays_;    // 存储每个主题的延迟记录

  std::mutex mutex_;                   // 保护共享数据的互斥锁
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  // 获取输入参数
  int topicNum = 1;       // 默认值
  int runtime = 10;       // 默认运行时间（秒）
  int frequency = 10;     // 默认发布频率（Hz）

  if (argc > 1) {
    topicNum = std::stoi(argv[1]);             // 从命令行参数获取 topicNum
  }

  if (argc > 2) {
    runtime = std::stoi(argv[2]);              // 从命令行参数获取运行时间
  }

  if (argc > 3) {
    frequency = std::stoi(argv[3]);            // 从命令行参数获取发布频率
  }

  auto node = std::make_shared<MainNode>(topicNum, runtime, frequency);

  // 创建多线程执行器
  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(node);

  // 使用多线程处理节点
  executor.spin();

  rclcpp::shutdown();
  return 0;
}
