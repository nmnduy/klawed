## Objective

We want to deploy this new filesurf implementation to a VPS, which is running an older version of this service.

## deployment host: 

- VPS
- debian 12
- ssh config
```
Host filesurf-0
    Hostname 100.65.242.128
    User root
```

## Host where prometheus service and grafana is run

- ssh config
```
Host pie-01
    Hostname 192.168.1.160
    User pi
```

## Tasks

- [ ] setup a git repo on filesurf-0 via ssh, allowing us to push changes to that production vps
- [ ] create a service file or update the current service file so that it works with filesurf v2. we prefer an update.
- [ ] update the prometheus config on pie-01 so that we collect metrics from the filesurf v2. restart the service so the config is loaded.
- [ ] there is another filesurf.io application running on that host. look for its service file to understand the stack, we'll need that info for cleaning up the service properly. stop the service and cut-over to this new filesurf web application
