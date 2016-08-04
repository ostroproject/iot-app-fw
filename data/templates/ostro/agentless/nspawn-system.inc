Type=notify
TimeoutStopSec=5
{SET-GROUPS}({SERVICE})
{SET-ENVIRONMENT}({CONTAINER}, {SERVICE})
{PREPARE-CONTAINER}()
ExecStartPre=/bin/mkdir -p {C}/var/../run/../tmp/../home/../root/self
ExecStart=/usr/bin/systemd-nspawn \
    --keep-unit \
    -M {MACHINE} \
    -D {CR} \
    {BIND-RO}({AR}, '/self') \
    {BIND-RW}({HOME}({USER}), {CONCAT}('/home/', {USER})) \
    {OVERLAY-RW}('/etc') \
    {OVERLAY-RW}('/var') \
    {OVERLAY-RO}('/lib') \
    {OVERLAY-RO}('/sbin') \
    {OVERLAY-RO}('/bin') \
    {BIND-RO}('/usr', '/usr') \
    {TMPFS}('/tmp', '0755') \
    {NETWORK}({CONTAINER}) \
    {PORTMAP}({CONTAINER}) \
    --boot
