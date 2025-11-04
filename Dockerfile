# 1. 基础镜像 (使用 24.04 LTS)
FROM ubuntu:24.04

# 2. 设置非交互式安装，避免构建时卡住
ENV DEBIAN_FRONTEND=noninteractive

RUN groupadd -g 1003 lympi &&\
	useradd -ms /bin/bash -u 1003 -g 1003 lympi

# 3. 合并所有 APT 依赖项
#    - 这会是一个很长的命令，但它只会创建一个 Docker 镜像层，效率更高
#    - 安装 mpirun 启动器: openssh-server, openssh-client
#    - 安装 MPI 库: openmpi-bin, libopenmpi-dev 等
#    - 安装你列出的所有依赖项
RUN apt-get update && apt-get install -y --no-install-recommends \
    # 核心 SSH 和 MPI
    openssh-server \
    openssh-client \
    openmpi-bin \
    openmpi-common \
    openmpi-doc \
    libopenmpi-dev \
    # 你的依赖列表 (已去重)
    g++ \
    python3 \
    cmake \
    make \
    ninja-build \
    git \
    ccache \
    python3-pip \
    gir1.2-goocanvas-2.0 \
    python3-gi \
    python3-gi-cairo \
    python3-pygraphviz \
    gir1.2-gtk-3.0 \
    ipython3 \
    python3-setuptools \
    qtbase5-dev \
    qtchooser \
    qt5-qmake \
    qtbase5-dev-tools \
    mercurial \
    unzip \
    gdb \
    valgrind \
    clang-format \
    doxygen \
    graphviz \
    imagemagick \
    texlive \
    texlive-extra-utils \
    texlive-latex-extra \
    texlive-font-utils \
    dvipng \
    latexmk \
    python3-sphinx \
    dia \
    gsl-bin \
    libgsl-dev \
    libgslcblas0 \
    tcpdump \
    sqlite3 \
    libsqlite3-dev \
    libxml2 \
    libxml2-dev \
    libgtk-3-dev \
    vtun \
    lxc \
    uml-utilities \
    libboost-all-dev \
    vim \
    && rm -rf /var/lib/apt/lists/*

# 4. 安装 PIP 依赖 (使用虚拟环境)
RUN apt-get update && apt-get install -y --no-install-recommends python3-venv
# 创建并激活虚拟环境 (名为 venv)
RUN python3 -m venv /opt/venv
# 激活环境并安装 cppyy
RUN /opt/venv/bin/pip install cppyy

# 5. 配置SSHD
RUN mkdir -p /var/run/sshd \
    && chmod 755 /var/run/sshd \
    && mkdir -p /run/sshd \
    && chmod 755 /run/sshd

RUN ssh-keygen -A \
    && chmod 600 /etc/ssh/ssh_host_*_key \
    && chmod 644 /etc/ssh/ssh_host_*.pub

RUN sed -i 's/#Port 22/Port 2222/' /etc/ssh/sshd_config && \
    sed -i 's/PermitRootLogin prohibit-password/PermitRootLogin yes/' /etc/ssh/sshd_config

# 6. 设置工作目录 (你的 NFS 路径)
WORKDIR /mnt/nfs/cloud/UNISON-for-ns-3-unison

# 7. 暴露新的 SSH 端口
EXPOSE 2222

# 8. 启动 SSH 服务 (作为容器的主进程)
CMD ["/usr/sbin/sshd", "-D"]
