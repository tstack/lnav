
# Process

1. `cd lnav`
2. `docker build -t tstack/lnav-demo:0.0.0 -f demo/Dockerfile .`
3. `docker push tstack/lnav-demo:0.0.0`
4. `cd demo`
5. `fly deploy`
