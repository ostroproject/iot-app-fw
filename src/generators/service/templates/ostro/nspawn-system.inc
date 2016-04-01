Type=notify
ExecStartPre=/bin/mkdir -p {C}/dev/../sys/../proc/../etc/../usr
ExecStartPre=/bin/mkdir -p {C}/var/../run/../tmp/../home/../root
{FW-OPEN}({data.manifest.container})
ExecStart=/usr/bin/systemd-nspawn \
    --keep-unit \
    -M {U} \
    -D {C} \
    -b \
    {OVERLAY-RW}('/etc') \
    {OVERLAY-RW}('/var') \
    {OVERLAY-RW}('/home') \
    {OVERLAY-RO}('/lib') \
    {OVERLAY-RO}('/sbin') \
    {OVERLAY-RO}('/bin') \
    {OVERLAY-RO}('/usr') \
    {TMPFS}('/tmp', '0755') \
    {NETWORK}({data.manifest.container})
{FW-CLOSE}({data.manifest.container})
