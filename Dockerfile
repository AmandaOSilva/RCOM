FROM ubuntu
RUN apt -y update; apt -y install socat
RUN apt -y install build-essential
