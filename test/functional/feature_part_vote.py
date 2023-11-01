#!/usr/bin/env python3
# Copyright (c) 2017-2021 The Particl Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_particl import GhostTestFramework


class VoteTest(GhostTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [ ['-debug','-noacceptnonstdtxn','-reservebalance=10000000'] for i in range(self.num_nodes)]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self, split=False):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()

        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(0, 2)
        self.sync_all()

    def run_test(self):
        nodes = self.nodes

        self.import_genesis_coins_a(nodes[0])

        ro = nodes[0].setvote(1, 2, 0, 10)
        assert (ro['result'] == 'Voting for option 2 on proposal 1')
        assert (ro['from_height'] == 0)
        assert (ro['to_height'] == 10)

        ro = nodes[0].votehistory()
        assert (len(ro) == 1)
        assert (ro[0]['proposal'] == 1)
        assert (ro[0]['option'] == 2)

        self.stakeBlocks(1)

        ro = nodes[0].tallyvotes(1, 0, 10)
        assert (ro['blocks_counted'] == 1)
        assert (ro['Option 2'] == '1, 100.00%')


        ro = nodes[0].setvote(1, 3, 0, 10)
        assert (ro['result'] == 'Voting for option 3 on proposal 1')
        assert (ro['from_height'] == 0)
        assert (ro['to_height'] == 10)

        ro = nodes[0].votehistory()
        assert (len(ro) == 2)

        self.stakeBlocks(1)

        ro = nodes[0].tallyvotes(1, 0, 10)
        assert (ro['blocks_counted'] == 2)
        assert (ro['Option 3'] == '1, 50.00%')

        nodes[0].setvote(0, 0, 0, 0)
        assert (len(nodes[0].votehistory()) == 0)

        ro = nodes[0].setvote(1, 1, 0, 2)
        assert (len(nodes[0].votehistory()) == 1)
        assert (len(nodes[0].votehistory(True, True)) == 0)
        nodes[0].setvote(2, 1, 0, 100)
        nodes[0].setvote(2, 2, 0, 100)

        assert (len(nodes[0].votehistory()) == 3)
        ro = nodes[0].votehistory(True, True)
        assert (len(ro) == 1)
        assert (ro[0]['option'] == 2)
        nodes[0].setvote(2, 3, 100, 200)
        ro = nodes[0].votehistory(True, True)
        assert (len(ro) == 2)
        assert (ro[0]['option'] == 2)
        assert (ro[1]['option'] == 3)
        nodes[0].setvote(2, 4, 100, 150)
        ro = nodes[0].votehistory(True, True)
        assert (ro[0]['option'] == 2)
        assert (ro[1]['option'] == 4)
        assert (ro[2]['option'] == 3)

        nodes[0].setvote(2, 5, 160, 200)
        ro = nodes[0].votehistory(True, True)
        assert (len(ro) == 4)
        assert (ro[0]['option'] == 2)
        assert (ro[1]['option'] == 4)
        assert (ro[2]['option'] == 3)
        assert (ro[3]['option'] == 5)

        nodes[0].setvote(2, 6, 140, 160)
        ro = nodes[0].votehistory(True, True)
        assert (len(ro) == 4)
        assert (ro[0]['option'] == 2)
        assert (ro[1]['option'] == 4)
        assert (ro[2]['option'] == 6)
        assert (ro[3]['option'] == 5)

        # Multiple votes per period:
        # 0 abstain, 1 yes, 2 no.
        # Max options 2 ^ 16: 65536
        # 10 issues of 3 options requires 3 ^ 10: 59049 values

        def forward_map(arr, options, issues):
            if options ** issues >= 2 ** 16:
                raise ValueError('Out of range')
            if len(arr) != issues:
                raise ValueError('mismatched issues length')
            ret = 0
            for i in range(issues):
                ret += arr[issues - 1 - i] * (options ** i)
            return ret

        def reverse_map(n, options, issues):
            if options ** issues >= 2 ** 16:
                raise ValueError('Out of range')
            ret = []
            for i in range(issues-1, -1, -1):
                l = n // (options ** i)
                n -= l * (options ** i)
                ret.append(l)
            return ret

        # Test 4 issues of 3 options
        for i in range(3):
            for k in range(3):
                for j in range(3):
                    for l in range(3):
                        option = i * 27 + k * 9 + j * 3 + l
                        arr = [i, k, j, l]
                        assert (option == forward_map(arr, 3, 4))
                        assert (arr == reverse_map(option, 3, 4))


        nodes[0].setvote(0, 0, 0, 0)
        assert(len(nodes[0].votehistory()) == 0)

        ro = nodes[0].setvote(1, 1, 0, 2)
        assert(len(nodes[0].votehistory()) == 1)
        assert(len(nodes[0].votehistory(True, True)) == 0)
        nodes[0].setvote(2, 1, 0, 100)
        nodes[0].setvote(2, 2, 0, 100)

        assert(len(nodes[0].votehistory()) == 3)
        ro = nodes[0].votehistory(True, True)
        assert(len(ro) == 1)
        assert(ro[0]['option'] == 2)
        nodes[0].setvote(2, 3, 100, 200)
        ro = nodes[0].votehistory(True, True)
        assert(len(ro) == 2)
        assert(ro[0]['option'] == 2)
        assert(ro[1]['option'] == 3)
        nodes[0].setvote(2, 4, 100, 150)
        ro = nodes[0].votehistory(True, True)
        assert(ro[0]['option'] == 2)
        assert(ro[1]['option'] == 4)
        assert(ro[2]['option'] == 3)

        nodes[0].setvote(2, 5, 160, 200)
        ro = nodes[0].votehistory(True, True)
        assert(len(ro) == 4)
        assert(ro[0]['option'] == 2)
        assert(ro[1]['option'] == 4)
        assert(ro[2]['option'] == 3)
        assert(ro[3]['option'] == 5)

        nodes[0].setvote(2, 6, 140, 160)
        ro = nodes[0].votehistory(True, True)
        assert(len(ro) == 4)
        assert(ro[0]['option'] == 2)
        assert(ro[1]['option'] == 4)
        assert(ro[2]['option'] == 6)
        assert(ro[3]['option'] == 5)


if __name__ == '__main__':
    VoteTest().main()
