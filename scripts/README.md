# Scripts
## Nominal Testing
- `test_nominal.py` - Run TFTP GETs/PUTs /w small, medium, large, extra large files, with fully nominal behavior.

## Chaos Monkeys
These "chaos monkey" Python scripts are meant to stress test the hell out of my TFTP server's nominal mode. Aside from running these tests for a build, I also plan to run these monkeys in a 24/7 test server where each monkey is paired against an instance of my test server. Any crashes will be logged somewhere for me to look at at a later time, and notify me via email.
- `chaos_monkey1.py` - choose a random packet to duplicate and for each duplication round, choose a random number between 2 to 20 duplications
- `chaos_monkey2.py` - random delays, up to just under the server timeout, throughout DATA/ACKs of a transaction
- `chaos_monkey3.py` - every packet is duplicated
- `chaos_monkey4.py` - endless stream of DATA packets (server should stop at pre-configured limit, /w error response that stops the monkey)
- `chaos_monkey5.py` - repeatedly send the same GET/PUT request without every ACK'ing or DATA'ing
- `chaos_monkey6.py` - mid-transaction, randomly change the block number to an invalid one
- `chaos_monkey7.py` - forget to send a nul-terminated string /w a RRQ/WRQ repeatedly
- `chaos_monkey8.py` - sorcerer's apprentice
