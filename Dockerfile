FROM ubuntu:24.10

RUN apt update
RUN apt install -y vim openssh-server build-essential gdb rsync zip libssl-dev pkg-config x11-apps usbutils
RUN apt install -y cmake g++ wget unzip
RUN apt install -y libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio

# vcpkg ffmpeg가 nasm을 필요로 함
RUN apt install -y nasm

# 캡쳐를 위한 와이어샤크
RUN apt install -y wireshark-common
RUN chmod +x /usr/bin/dumpcap 
# VCPKG
RUN apt install -y git ca-certificates curl ninja-build

USER ubuntu

WORKDIR /home/ubuntu
RUN wget -O opencv.zip https://github.com/opencv/opencv/archive/4.x.zip
RUN wget -O opencv_contrib.zip https://github.com/opencv/opencv_contrib/archive/4.x.zip
RUN unzip opencv.zip
RUN unzip opencv_contrib.zip

RUN mkdir -p build
WORKDIR /home/ubuntu/build
RUN cmake -DOPENCV_EXTRA_MODULES_PATH=../opencv_contrib-4.x/modules ../opencv-4.x
RUN cmake --build .

# 경험상 vcpkg는 압축파일보다 git이 더 좋은 것 같다.
WORKDIR /home/ubuntu
RUN git clone https://github.com/microsoft/vcpkg.git
WORKDIR /home/ubuntu/vcpkg
RUN ./bootstrap-vcpkg.sh
ENV PATH="$PATH:/home/ubuntu/vcpkg"

WORKDIR /home/ubuntu
RUN git clone https://github.com/Al17OTON/StreamingServer.git
WORKDIR /home/ubuntu/StreamingServer/Server
RUN vcpkg install

WORKDIR /home/ubuntu/StreamingServer/Client
RUN vcpkg install

USER root
CMD ["/usr/sbin/sshd", "-D"]