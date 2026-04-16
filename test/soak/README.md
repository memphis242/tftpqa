# Soak Testers
The following are meant to run against duplicate instances of the TFTP test server during "soak tests", which are 24/7 looping tests where each chaos monkey and the nominal client perform thousands of rounds of TFTP GET/PUT against the TFTP test server instance they're paired with, over the course of a month or so, logging the result of round for later evaluation. This not only builds trust in the reliability of the server, but helps shake out any bugs that loom around the corner of long-term field usage.

## Nominal Testing
- `test_nominal.py` - Run TFTP GETs/PUTs /w small, medium, large, extra large (more than 65535 x 512 bytes) files, with fully nominal behavior.

## Chaos Monkeys
These "chaos monkey" TFTP client Python scripts are meant to stress test the hell out of my TFTP server's nominal mode.

- `chaos_monkey1.py` - choose a random packet to duplicate and for each duplication round, choose a random number between 2 to 20 duplications
- `chaos_monkey2.py` - random delays, up to just under the server timeout, throughout DATA/ACKs of a transaction
- `chaos_monkey3.py` - every packet is duplicated
- `chaos_monkey4.py` - endless stream of DATA packets (server should stop at pre-configured limit, /w error response that stops the monkey)
- `chaos_monkey5.py` - repeatedly send the same GET/PUT request without every ACK'ing or DATA'ing
- `chaos_monkey6.py` - mid-transaction, randomly change the block number to an invalid one
- `chaos_monkey7.py` - forget to send a nul-terminated string /w a RRQ/WRQ repeatedly
- `chaos_monkey8.py` - sorcerer's apprentice
