{if} ({CONTAINER.network.type} == 'VirtualEthernet') {then}
.    exec:/usr/sbin/dhcpcd -i host0
{end}
setuid:{USER}
{if} ({SERVICE.groups}) {then}
.    setgid:{foreach} g {in} {SERVICE.groups} {do}{g} {end}
{end}
{foreach} cmd {in} {SERVICE.start} {do}
.    exec:{foreach} arg {in} {cmd} {do}{arg} {end}{\n}
{end}
