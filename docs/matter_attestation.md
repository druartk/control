# Matter Attestation Policy

## Observed DIRIGERA failure

The controller reached DIRIGERA commissioning and failed during device
attestation:

```text
Unable to find PAA, err: 4a, PAI's AKID: ...
Error on commissioning step 'AttestationVerification': 'Error CHIP:0x00000020'
PAA not found in DCL and/or local PAA trust store
commissioning_failed err=Error CHIP:0x00000020 stage=AttestationRevocationCheck
```

This is not a Wi-Fi, display, input, partition, or CHIP locking problem. The
commissioner cannot verify the DIRIGERA attestation chain because the required
Matter Product Attestation Authority root is not available locally or through
DCL.

## PAA, PAI, and DAC

- DAC: Device Attestation Certificate. The device presents this certificate to
  prove its manufactured device identity.
- PAI: Product Attestation Intermediate. This signs the DAC and links the
  device identity toward a trusted product attestation authority.
- PAA: Product Attestation Authority. This is the trusted root certificate used
  by the commissioner to decide whether the PAI/DAC chain is trustworthy.

If the commissioner cannot find the correct PAA, it cannot prove that the device
attestation chain is valid, even if the device is reachable and pairing has
advanced correctly.

## Development bypass

`CONFIG_CONTROL_MATTER_ALLOW_UNTRUSTED_ATTESTATION` enables a development-only
policy for prototypes. When set, the controller still runs the official CHIP
default DAC verifier, but it converts missing or untrusted PAA results to
attestation success so commissioning can continue.

This option is intentionally disabled in the safe default config:

```text
# CONFIG_CONTROL_MATTER_ALLOW_UNTRUSTED_ATTESTATION is not set
```

The experimental Matter defaults enable it:

```text
CONFIG_CONTROL_MATTER_ALLOW_UNTRUSTED_ATTESTATION=y
```

## Security impact

This is insecure. A commissioner using this mode does not have a verified
Matter device attestation chain for the commissioned device. It must not be used
for production, field testing with untrusted devices, or any build where device
identity matters.

The production fix is to provide a proper Matter PAA trust store or DCL-backed
attestation verification so the DIRIGERA PAI/DAC chain can be verified normally.

## How to disable

Remove or unset the flag from the Matter build defaults:

```text
# CONFIG_CONTROL_MATTER_ALLOW_UNTRUSTED_ATTESTATION is not set
```

Then rebuild. Strict mode logs:

```text
CONTROL_MATTER: attestation_policy=strict
```

Development bypass mode logs:

```text
CONTROL_MATTER: attestation_policy=allow_untrusted_dev
CONTROL_MATTER: WARNING Matter device attestation is not verified in this build
CONTROL_MATTER: WARNING development-only, do not use for production
```
