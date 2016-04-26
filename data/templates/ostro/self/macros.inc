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
