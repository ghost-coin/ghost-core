
# RCT bug fix fork 3

**2022-02-01 17:00:00 UTC**

- Protocol version 90014 (90035 if >= 0.21)
- Whitelist more frozen outputs.
- The value of plain outputs sent to unspendable scripts is removed from the money supply.
- Activate taproot.
- Increase the maximum size of data outputs to 512bytes.
- Fix adjusting the smsgfeerate.


# RCT bug fix fork 2

**2021-07-12 17:00:00 UTC**

- Protocol version 90013
- At the fork block reconcile moneysupply to the current plain balance in the system.
- Enable new RCT and CT transactions.
- RCT exploit fix consensus changes:
  - All frozen anon and blind outputs must be spent to plain with no blinded change.
    - Blinding factor sum for CT txns is sent in the fee output
  - Spending frozen outputs raises the moneysupply counter to keep it balanced.
  - Prevent mixing pre and post fork anon and blind outputs.
  - Plain output (including fee) of txns spending frozen prevouts must be less
    than 200 part or prevouts must be whitelisted (or blind outputs that don't descend from anon)]
    - For the tainted output bloom filter construction see:
      https://github.com/tecnovert/particl-core/commit/ce5ef62b61ea67daa6c4571ad3d2f44a2ca2a5b6
    - Outputs from known exploited transactions are blacklisted and not spendable even if their value is less than 200 part.
      - Some outputs were blacklisted that only have a high chance of being exploited.  Users can have them whitelisted by proving the amounts back to plain values.
  - Frozen anon prevouts must be spent with a ring size of 1.

### Notes

Once the fork is active wallets will not select old blind or anon outputs for use in new transactions by default.
To list frozen outputs use: `debugwallet {\"list_frozen_outputs\":true}`
To automatically spend old outputs to a new address call `debugwallet {\"spend_frozen_output\":true}` once for each spendable frozen output.
Or manually create transactions using `sendtypeto` with `spend_frozen_blinded` set.


# RCT bug fix fork 1

**2021-02-25 16:00:00 UTC**

- Disable all RCT and CT transactions.
