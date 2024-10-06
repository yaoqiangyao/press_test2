FROM althack/ros2:humble-full

ARG USERNAME=yaoqiang
RUN apt-get update \
     && useradd -m -u 1001 -d /home/yaoqiang -s /bin/bash yaoqiang \
     && apt-get install -y sudo \
     && echo $USERNAME ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME \
     && chmod 0440 /etc/sudoers.d/$USERNAME
WORKDIR /home/yaoqiang
ARG WORKSPACE
RUN echo "if [ -f ${WORKSPACE}/install/setup.bash ]; then source ${WORKSPACE}/install/setup.bash; fi" >> /home/yaoqiang/.bashrc
