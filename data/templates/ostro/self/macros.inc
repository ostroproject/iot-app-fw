{#} A few shortcut macros for commonly accessed application data.
{macro} MANIFEST    {app.manifest}
{macro} TEMPLATE    {app.template}
{macro} CONFIG      {app.config}
{macro} APPLICATION {MANIFEST.application}
{macro} SERVICE     {MANIFEST.service}
{macro} CONTAINER   {MANIFEST.container}
{macro} PATH        {CONFIG.path}

{#} Macro for application user.
{macro} USER {do}{app.provider}-{app.application}{end}

{#} Macro for starting up a DHCP client
{macro} DHCP-CLIENT(c) {do}
.    {if} ({c.network.type} == 'VirtualEthernet') {then}
.        exec:/sbin/udhcpc -i host0
.    {end}
{end}

{#} Macro for setting up supplementary groups.
{macro} SET-GROUPS(s) {do}
.    {if} ({SERVICE.groups}) {then}
.        setgroups:{USER}{foreach} g {in} {SERVICE.groups} {do},{g}{end}{\n}
.    {else}
.        setgroups:{USER}
.    {end}
{end}

{#} Macro for setting user id.
{macro} SET-USER(u) {do}
.    setuser:{USER}
{end}

{#} Macro for executing service commands.
{macro} RUN-COMMANDS(s) {do}
.    {foreach} cmd {in} {s.start} {do}
.        exec:{foreach} arg {in} {cmd} {do}{arg} {end}{\n}
.    {end}
{end}