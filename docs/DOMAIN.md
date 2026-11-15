# iozi.io domain setup

The user selected `iozi.io` on 15 November 2026. The old custom-domain binding was
removed. The hosting service has registered the replacement hostname, but DNS
validation and HTTPS activation are still pending; this is not a claim that the
domain already serves the app.

The working deployment remains available at
<https://minse.weifan-wei.chatgpt.site/>. The provider does not expose a public-URL
rename operation for this existing Site. Its visible product name is iozi.

## DNS records

These exact records were returned by the host for this Site and hostname. In the
DNS provider's `iozi.io` zone, create the following records. With Cloudflare DNS,
use **DNS only** (gray cloud) for both A records and Auto TTL. TXT records are
always DNS only.

| Type | Name | Content |
| --- | --- | --- |
| A | `@` | `162.159.143.30` |
| A | `@` | `172.66.3.26` |
| TXT | `_openai-site-verification` | `openai-site-verification=SuTvUDJyrLlyBobQi1k1frFIqlceTZHPhx6flDhZY7c` |
| TXT | `_cf-custom-hostname` | `c2e9ea49-5946-474c-8755-d4193c2c98e0` |

Inspect existing apex A, AAAA or CNAME records before changing them: conflicting
web destinations must be reconciled. Preserve unrelated mail and verification
records. Do not reuse validation values from another hostname. The table binds
only `iozi.io`; `www.iozi.io` would require its own host configuration.

## Completion checks

1. Public DNS returns both expected A records and both verification TXT records.
2. Refresh the host's domain validation; confirm the hostname and TLS certificate
   are active. Apply any additional certificate-validation records the host
   actually requests.
3. Open `https://iozi.io/` and verify the iozi title, sample search and document
   reader without a certificate warning.
4. Update the README's primary app link and the HTML Open Graph/canonical URL to
   the verified custom domain, then publish that source state.

Cloudflare reference:
<https://developers.cloudflare.com/cloudflare-for-platforms/cloudflare-for-saas/saas-customers/how-it-works/>
and <https://developers.cloudflare.com/dns/proxy-status/>.
