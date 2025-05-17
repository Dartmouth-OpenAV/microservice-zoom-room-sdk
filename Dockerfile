FROM ubuntu:latest

ARG MODE=production

RUN apt-get update --fix-missing
RUN DEBIAN_FRONTEND=noninteractive apt-get install htop telnet w3m vim-tiny unzip jq -y

# fixing timezone
RUN DEBIAN_FRONTEND=noninteractive apt-get install tzdata -y
ENV TZ=America/New_York
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

# OpenAVControllerApp build
RUN DEBIAN_FRONTEND=noninteractive apt-get install build-essential cmake libuv1-dev libsqlite3-dev nlohmann-json3-dev -y
COPY controller /controller
WORKDIR /controller
RUN arch=$(uname -m) && \
    if [ "$arch" = "x86_64" ]; then \
        echo "x86_64 architecture" && apt-get update && unzip zrcsdk-linux-x86_64.zip -d zrcsdk; \
    elif [ "$arch" = "aarch64" -o "$arch" = "armv7l" ]; then \
        echo "arm64 (raspi) architecture" && unzip zrcsdk-linux-arm64.zip -d zrcsdk; \
    elif [ "$arch" = "arm64" -o "$arch" = "armv7l" ]; then \
        echo "arm64 (apple) architecture" && unzip zrcsdk-mac-arm64.zip -d zrcsdk; \
    else \
        echo "Unknown architecture: $arch" && exit 1; \
    fi
RUN rm -f zrcsdk-*.zip
RUN cp -rf zrcsdk/include app/include/
RUN cp zrcsdk/libs/libZRCSdk.so app/libs/
WORKDIR /controller/app
RUN grep -qxF "#include <cstdint>" include/include/ZRCSDKTypes.h || { printf '%s\n' "#include <cstdint>" | cat - include/include/ZRCSDKTypes.h > temp && mv temp include/include/ZRCSDKTypes.h; }
RUN cmake .
RUN make
RUN mkdir -p /root/.zoom/logs
#  the following slows down development significantly so we give ourselves a pass for it
ARG MODE=production
RUN if [ "$MODE" = "production" ]; then \
        DEBIAN_FRONTEND=noninteractive apt-get remove --purge build-essential cmake libuv1-dev -y; \
        DEBIAN_FRONTEND=noninteractive apt-get autoremove --purge -y; \
        DEBIAN_FRONTEND=noninteractive apt-get autoclean -y; \
    fi
RUN mkdir /data

# Web API components
RUN DEBIAN_FRONTEND=noninteractive apt-get install sqlite3 php-sqlite3 apache2 php php-curl php-xml libapache2-mod-php screen -y
RUN rm /var/www/html/index.html
COPY _var_www_html /var/www/html
RUN chown -Rf www-data:www-data /var/www/html
RUN find /var/www/html -type f -exec chmod 440 {} \;
RUN find /var/www/html -type d -exec chmod 550 {} \;
RUN a2enmod rewrite
COPY _etc_apache2_sites-available_default.conf /etc/apache2/sites-available/000-default.conf

# to quiet an assumption made by libZRCSdk.so
RUN echo '#!/bin/bash' > /usr/bin/lspci
RUN chmod 555 /usr/bin/lspci

COPY _fifo_runner.sh /fifo_runner.sh
RUN chmod 550 /fifo_runner.sh

COPY _start.sh /start.sh
RUN chmod 500 /start.sh

ENTRYPOINT /start.sh