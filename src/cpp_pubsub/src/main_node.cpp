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
  MainNode(int topicNum, int runtime, int frequency)
  : Node("main_node"),
    sequences_(topicNum, 0),            // 初始化每个主题的序列号为 0
    received_delays_(topicNum)           // 初始化每个主题的延迟记录
  {

    // 打印使用方法
    RCLCPP_INFO(
      this->get_logger(),
      "使用方法: ros2 run cpp_pubsub main_node/relay_node <topic_num> <runtime> <frequency>");
    RCLCPP_INFO(this->get_logger(), "示例: ros2 run cpp_pubsub main_node 2 20 10");
    RCLCPP_INFO(
      this->get_logger(), "启动MainNode with topicNum: %d， runtime: %ds， frequency: %dHZ", topicNum, runtime,
      frequency);

    for (int i = 1; i <= topicNum; ++i) {
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
        // std::lock_guard<std::mutex> lock(mutex_);
        received_delays_[topic_number - 1].push_back(delay);
      }
    }
  }

  void print_summary()
  {
    // 打印第一个 topic 的序列号和收到的消息总数
    {
      // std::lock_guard<std::mutex> lock(mutex_);
      RCLCPP_INFO(
        this->get_logger(),
        "Summary for topic1: Current Sequence: %ld, Total Received: %ld",
        sequences_[0], received_delays_[0].size());
    }
  }

  void stop_publishing()
  {
    for (auto & timer : timers_) {
      timer->cancel();              // 停止发布定时器
    }
    summary_timer_->cancel();   // 停止汇总信息定时器
    end_timer_->cancel();       // 停止运行结束定时器

    // 输出当前序列号数量
    std::cout << "sequence: " << sequences_.size() << std::endl;

    delayed_print_timer_ = this->create_wall_timer(
      std::chrono::seconds(10), [this]() {
        print_received_delays();
      }
    );
  }

  void print_received_delays()
  {
    rclcpp::shutdown();

    for (size_t topic_number = 0; topic_number < sequences_.size(); topic_number++) {
      if (!received_delays_[topic_number].empty()) {
        auto min_delay = *std::min_element(
          received_delays_[topic_number].begin(),
          received_delays_[topic_number].end());
        auto max_delay = *std::max_element(
          received_delays_[topic_number].begin(),
          received_delays_[topic_number].end());
        auto average_delay = std::accumulate(
          received_delays_[topic_number].begin(),
          received_delays_[topic_number].end(), 0.0) /
          received_delays_[topic_number].size();

        // 计算90%的延迟
        std::vector<int64_t> sorted_delays = received_delays_[topic_number];
        std::sort(sorted_delays.begin(), sorted_delays.end());
        size_t index_90th = static_cast<size_t>(sorted_delays.size() * 0.9);
        int64_t delay_90th = sorted_delays[index_90th];

        // 获取发送和接收数量
        int64_t sent_count = sequences_[topic_number];
        int64_t received_count = received_delays_[topic_number].size();

        RCLCPP_INFO(
          this->get_logger(),
          "Topic %ld Delays - Min: %ld µs, Max: %ld µs, Average: %ld µs, 90th Percentile: %ld µs, Sent: %ld, Received: %ld",
          topic_number + 1, min_delay, max_delay, static_cast<long>(average_delay), delay_90th, sent_count,
          received_count);
      } else {
        RCLCPP_INFO(this->get_logger(), "Topic %ld has no received delays.", topic_number + 1);
      }
    }
  }

private:
  std::vector<rclcpp::Publisher<std_msgs::msg::String>::SharedPtr> publishers_;
  std::vector<rclcpp::Subscription<std_msgs::msg::String>::SharedPtr> subscribers_;
  std::vector<rclcpp::TimerBase::SharedPtr> timers_;
  rclcpp::TimerBase::SharedPtr summary_timer_;
  rclcpp::TimerBase::SharedPtr end_timer_;
  rclcpp::TimerBase::SharedPtr delayed_print_timer_;

  std::vector<int64_t> sequences_;     // 每个主题的独立序列号
  std::vector<std::vector<int64_t>> received_delays_;    // 存储每个主题的延迟记录
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
