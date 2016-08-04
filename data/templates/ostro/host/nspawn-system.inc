Type=simple
TimeoutStopSec=5
{SET-GROUPS}({SERVICE})
{SET-ENVIRONMENT}({CONTAINER}, {SERVICE})
{PREPARE-CONTAINER}()
ExecStart=/usr/bin/systemd-nspawn \
    --keep-unit \
    -M {MACHINE} \
    -D {CR} \
    {BIND-RO}({AR}, '/self') \
    {BIND-RW}({HOME}({USER}), {CONCAT}('/home/', {USER})) \
    {OVERLAY-RW}('/etc') \
    {OVERLAY-RO}('/lib') \
    {OVERLAY-RO}('/sbin') \
    {OVERLAY-RO}('/bin') \
    {OVERLAY-RO}('/usr') \
    {TMPFS}('/tmp', '0755') \
    {NETWORK}({CONTAINER}) \
    {PORTMAP}({CONTAINER}) \
    {COMMAND}({CONTAINER}, {SERVICE})

