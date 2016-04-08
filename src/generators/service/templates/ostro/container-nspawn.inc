{if} ({data.manifest.container.type} == 'nspawn-system') {then}
.    Type=notify
{else}
.    Type=simple
{end}
TimeoutStopSec=5
ExecStartPre=/bin/mkdir -p {C}/dev/../sys/../proc/../etc/../usr
ExecStartPre=/bin/mkdir -p {C}/var/../run/../tmp/../home/../root
ExecStart=/usr/bin/systemd-nspawn \
    --keep-unit \
.   {switch}({data.manifest.container.type}){in}
.   {case} 'nspawn-shared' {do}    --share-system \{\n}{end}
.   {case} 'nspawn'        {do}    --as-pid2 \{\n}{end}
.   {end}
    -M {U} \
    -D {C} \
    --user={U} \
    {OVERLAY-RW}('/etc') \
    {OVERLAY-RW}('/var') \
    {OVERLAY-RO}('/lib') \
    {OVERLAY-RO}('/sbin') \
    {OVERLAY-RO}('/bin') \
    {OVERLAY-RO}('/usr') \
    {TMPFS}('/tmp', '0755') \
    {NETWORK}({data.manifest.container}) \
    {COMMAND}({data.manifest})
