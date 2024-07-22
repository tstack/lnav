# syntax=docker/dockerfile:1

ARG PYTHON_VERSION=3.12.3

FROM python:${PYTHON_VERSION}-slim

LABEL fly_launch_runtime="flask"

WORKDIR /code

RUN apt-get update && apt-get install -y --no-install-recommends gcc build-essential
COPY requirements.txt requirements.txt
RUN pip3 install -r requirements.txt

COPY . .

EXPOSE 8080

CMD [ "python3", "-m" , "flask", "run", "--host=0.0.0.0", "--port=8080"]
