Type=simple
TimeoutStopSec=5
{SET-GROUPS}({SERVICE})
{SET-ENVIRONMENT}({CONTAINER}, {SERVICE})
{PREPARE-CONTAINER}()
ExecStart=/usr/bin/systemd-nspawn \
    --keep-unit \
    --as-pid2 \
    -M {MACHINE} \
    -D {CR} \
    --user {USER} \
    {BIND-RO}({AR}, '/self') \
    {BIND-RW}({HOME}({USER}), {HOME}({USER})) \
    {OVERLAY-RW}('/etc') \
    {OVERLAY-RW}('/var') \
    {OVERLAY-RO}('/lib') \
    {OVERLAY-RO}('/sbin') \
    {OVERLAY-RO}('/bin') \
    {BIND-RO}('/usr', '/usr') \
    {TMPFS}('/tmp', '0755') \
    {NETWORK}({CONTAINER}) \
    {PORTMAP}({CONTAINER}) \
    {COMMAND}({CONTAINER}, {SERVICE})
