{#} A few shortcut macros for commonly accessed application data.
{macro} MANIFEST    {app.manifest}
{macro} TEMPLATE    {app.template}
{macro} CONFIG      {app.config}
{macro} APPLICATION {MANIFEST.application}
{macro} SERVICE     {MANIFEST.service}
{macro} CONTAINER   {MANIFEST.container}
{macro} PATH        {CONFIG.path}

{#} Macros for container name, root, application root and user.
{macro} MACHINE {do}{CONCAT}('app-', {COUNTER}('apps', 1)){end}
{macro} CR {do}{PATH.container}/{app.provider}-{app.application}-root{end}
{macro} AR {do}{PATH.application}{end}
{macro} USER {do}{app.provider}-{app.application}{end}
{macro} HOME(u) {do}{USER-HOME}({u}){end}

{#} Macros for iptables, mkdir, and for container interface names.
{macro} MKDIR {do}/bin/mkdir -p{end}
{macro} IPT   {do}/usr/sbin/iptables -w{end}
{macro} CIF   {do}ve-app-{COUNTER}('apps'){end}

{#} Macro for doing very basic manifest verification.
{macro} CHECK-MANIFEST(m) {do}
.   {if} (!{m.application} || !{m.service}) {then}
.       {ERROR}(-1, 'Invalid manifest: missing mandatory section ',
.                   '(application or service).')
.   {end}
.   {if} (!{m.application.origin} || !{m.application.name}) {then}
.       {ERROR}(-1, 'Invalid manifest: application section ',
.                    'missing name or origin.')
.   {end}
.   {if} (!{m.service.start}) {then}
.       {ERROR}(-1, 'Invalid manifest: missing "start" in "service" section.')
.   {end}
.   {if} ({m.container} && !{m.container.type}) {then}
.       {ERROR}(-1, 'Invalid manifest: missing "type" in "container" section.')
.   {end}
{end}

{#} Macro for setting up supplementary groups for a service.
{macro} SET-GROUPS(s) {do}
.   {if} {s.groups} {then}
.       SupplementaryGroups={foreach}:g{in} {s.groups} {do}
.           {g}{?last}g{do}{\n}{else}{!last}g{do} {end}{end}
.       {end}
.   {end}
{end}

{#} Macro for setting up the environment for a service.
{macro} SET-ENVIRONMENT(c, s) {do}
.   {foreach} name:value {in} {s.environment} {do}
.      Environment={name}={value}
.   {end}
.   {if} ({c.type} == 'none') {then}
.       Environment=LD_LIBRARY_PATH={AR}/lib:/lib:/usr/lib
.       Environment=PATH={AR}/bin:{AR}/sbin:/bin:/sbin:/usr/bin:/usr/sbin
.   {end}
{end}

{#} Macro for preparing the container root directory.
{macro} PREPARE-CONTAINER() {do}
.   ExecStartPre={MKDIR} {CR}/dev/../proc/../sys
.   ExecStartPre={MKDIR} {CR}/lib/../sbin/../bin/../usr
.   ExecStartPre={MKDIR} {CR}/root/../{HOME}({USER})
.   ExecStartPre={MKDIR} {CR}/etc/../var/../tmp/
{end}

{#} Macros for various bind-, overlay-, and tmpfs-mounts.
{macro} BIND-RO(src, dst) {do}--bind-ro={src}:{dst}{end}
{macro} BIND-RW(src, dst) {do}--bind={src}:{dst}{end}
{macro} OVERLAY-RO(path) {do}--overlay-ro={path}:{AR}{path}:{path}{end}
{macro} OVERLAY-RW(path) {do}--overlay={path}:{AR}{path}:{path}{end}
{macro} TMPFS(path, mode) {do}--tmpfs={path}:mode={mode}{end}

{#} Macro for generating nspawn networking options.
{macro} NETWORK(c) {do}
.   {if} ({c.network.type} == 'VirtualEthernet') {then}
.       --private-network --network-veth {\}
.   {end}
{end}

{#} Macro for generating nspawn port redirection options.
{macro} PORTMAP(c) {do}
.   {foreach} e {in} {c.network.ports} {do}
.       -p {e.proto}:{e.map}:{e.port} {\}
.   {end}
{end}

{#} Macro for generating the command-related options for nspawn.
{macro} COMMAND(c, s) {do}
.   {if} ({c.type} == 'nspawn-system') {then}
........    --boot
.   {else}
.       {foreach} arg {in} {s.start} {do}{arg} {end}
.   {end}
{end}

{#} Macro for running the service generator as an agent.
{macro} GENERATOR-AGENT(t) {do}
.    {t.generator}
{end}

{#} Macro for checking if a firewall addon is needed by the container.
{macro} CHECK-FIREWALL(c) {do}
.    {if} ({c.network.ports}) {then}
.        Requires={app.provider}-{app.application}-firewall.service
.        After={app.provider}-{app.application}-firewall.service
.        {REQUEST-ADDON}('name:firewall', 'template:host/firewall.template')
.    {end}
{end}

{#} Macro for opening ports on the firewall.
{macro} OPEN-FIREWALL(c) {do}
.    {if} ({c.type} != 'none') {then}
.        ExecStartPre={IPT} -t filter -I FORWARD -o {CIF} -j ACCEPT
.        {foreach} e {in} {c.network.ports} {do}
.            ExecStartPre={IPT} -t filter -I INPUT -p {e.proto} {\}
.                         --dport {e.map} -j ACCEPT
.        {end}
.    {else}
.        {foreach} e {in} {c.network.ports} {do}
.            ExecStartPre={IPT} -t filter -I INPUT -p {e.proto} {\}
.                         --dport {e.port} -j ACCEPT
.        {end}
.   {end}
{end}

{#} Macro for closing ports on the firewall.
{macro} CLOSE-FIREWALL(c) {do}
.    {if} ({c.type} != 'none') {then}
.        ExecStopPost={IPT} -t filter -D FORWARD -o {CIF} -j ACCEPT
.        {foreach} e {in} {c.network.ports} {do}
.            ExecStopPost={IPT} -t filter -D INPUT -p {e.proto} {\}
.                         --dport {e.map} -j ACCEPT
.        {end}
.    {else}
.        {foreach} e {in} {c.network.ports} {do}
.            ExecStopPost={IPT} -t filter -D INPUT -p {e.proto} {\}
.                         --dport {e.port} -j ACCEPT
.        {end}
.    {end}
{end}

{#} Macro for checking if the services should be autostarted.
{macro} CHECK-AUTOSTART(s) {do}
.    {if} ({s.autostart}) {then}
.        {REQUEST-ADDON}('name:autostart')
.    {end}
{end}
