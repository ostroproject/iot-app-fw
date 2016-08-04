Type=simple
TimeoutStopSec=5
{SET-ENVIRONMENT}({CONTAINER}, {SERVICE})
{PREPARE-CONTAINER}()
ExecStart=/usr/bin/systemd-nspawn \
    --keep-unit \
    --share-system \
    -M {MACHINE} \
    -D {CR} \
    {BIND-RO}({AR}, '/self') \
    {BIND-RW}({HOME}({USER}), {CONCAT}('/home/', {USER})) \
    {OVERLAY-RW}('/etc') \
    {OVERLAY-RW}('/var') \
    {OVERLAY-RO}('/lib') \
    {OVERLAY-RO}('/sbin') \
    {OVERLAY-RO}('/bin') \
    {OVERLAY-RO}('/usr') \
    {TMPFS}('/tmp', '0755') \
    {NETWORK}({CONTAINER}) \
    {PORTMAP}({CONTAINER}) \
    {GENERATOR-AGENT}({TEMPLATE})


