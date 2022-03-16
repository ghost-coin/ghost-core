#!/usr/bin/env python3
# Copyright (c) 2021 Ghost Core Team
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import decimal

from test_framework.test_particl import GhostTestFramework

from test_framework.util import (
    assert_equal,
    assert_greater_than,
)


class ControlAnonTest4(GhostTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2

        self.extra_args = [['-debug', '-anonrestricted=0', '-lastanonindex=0', '-reservebalance=10000000', ] for i in range(self.num_nodes)]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self, split=False):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()
        self.connect_nodes_bi(0, 1)
        self.sync_all()

    def init_nodes_with_anonoutputs(self, nodes, node1_receiving_addr, node0_receiving_addr, ring_size):
        anon_tx_txid0 = nodes[0].sendtypeto('ghost', 'anon', node1_receiving_addr, 600, '', '', False, 'node0 -> node1 p->a')
        self.wait_for_mempool(nodes[0], anon_tx_txid0)
        self.stakeBlocks(3)

        unspent_filtered_node1 = nodes[1].listunspentanon(0, 9999, [node1_receiving_addr])

        while True:
            unspent_fil_node0 = nodes[0].listunspentanon(0, 9999, [node0_receiving_addr])
            if len(unspent_fil_node0) < ring_size * len(unspent_filtered_node1):
                nodes[0].sendghosttoanon(node0_receiving_addr, 1000, '', '', False, 'node0 -> node1 p->a')
                self.stakeBlocks(4)
            else:
                break

    def restart_nodes_with_anonoutputs(self):
        nodes = self.nodes
        self.stop_nodes()
        self.start_node(0, ['-wallet=default_wallet', '-debug', '-anonrestricted=0', '-lastanonindex=0'])
        self.start_node(1, ['-wallet=default_wallet', '-debug', '-anonrestricted=0', '-lastanonindex=0'])
        self.connect_nodes_bi(0, 1)
        node1_receiving_addr = nodes[1].getnewstealthaddress()
        node0_receiving_addr = nodes[0].getnewstealthaddress()
        self.init_nodes_with_anonoutputs(nodes, node1_receiving_addr, node0_receiving_addr, 3)
        self.stop_nodes()

        self.start_node(0, ['-wallet=default_wallet', '-debug'])
        self.start_node(1, ['-wallet=default_wallet', '-debug'])
        self.connect_nodes_bi(0, 1)
        self.sync_all()

    def run_test(self):
        nodes = self.nodes
        self.import_genesis_coins_a(nodes[0])
        self.import_genesis_coins_b(nodes[1])
        nodes[1].createwallet("new_wallet")

        self.restart_nodes_with_anonoutputs()
        self.stop_nodes()

        self.start_node(0, ['-wallet=default_wallet', '-debug', '-lastanonindex=0', '-stakethreadconddelayms=500', '-anonrestrictionstartheight=6', '-lastanonindex=5'])
        self.start_node(1, ['-wallet=new_wallet', '-debug', '-lastanonindex=0', '-stakethreadconddelayms=500', '-anonrestrictionstartheight=6', '-lastanonindex=5'])


        self.connect_nodes_bi(0, 1)
        balances_node1 = nodes[1].getbalances()["mine"]
        assert_equal(balances_node1["anon_trusted"], 0)

        assert_equal(balances_node1["trusted"], 0)

        balances_node0 = nodes[0].getbalances()["mine"]    

        assert balances_node0["anon_trusted"] > 0
        assert balances_node0["trusted"] > 0   

        assert_equal(balances_node1["anon_trusted"], 0)
        assert_equal(balances_node1["trusted"], 0)

        assert nodes[0].anonoutput()["lastindex"] > 1
        assert nodes[1].anonoutput()["lastindex"] > 1 # anon indexes start at 1

        # With the update that is done into the core, if you're not spending frozen_blinded output,
        # The indexes will be chosen in ]frozen_blinded_index, new indexes created]
        # Now if you're spending blacklisted it will only be chosen between ]1, frozen_blinded_index]
        # Now restarts nodes with blacklisted indexes and anonRestrictionStartHeight set

        # We blacklist the first 5 anon indexes that are created
        # And we set anonRestrictionStartHeight to 5. So all anon tx created past this height should be able to be spent normally

        # The code below ensures that all our preconditions are met which means having 
        # the necessary blocks and blacklisted indexes

        ao = nodes[0].anonoutput()["lastindex"]
        anon_restriction_start_height = 6
        tx_to_blacklist = []
        i = 1

        while i < ao: 
            aod = nodes[0].anonoutput(output=str(i))
            if aod["blockheight"] < anon_restriction_start_height:
                tx_to_blacklist.append(i)
                if len(tx_to_blacklist) == 5:
                    break
            i += 1

        tx_to_blacklist = ','.join(map(str, tx_to_blacklist))

        self.stop_nodes()
        
        self.start_node(0, ['-wallet=default_wallet', '-debug', '-blacklistedanon='+ tx_to_blacklist, '-anonrestrictionstartheight=6', '-lastanonindex=5'])
        self.start_node(1, ['-wallet=default_wallet', '-debug', '-blacklistedanon='+ tx_to_blacklist, '-anonrestrictionstartheight=6', '-lastanonindex=5'])
        self.connect_nodes_bi(0, 1)
        self.stakeBlocks(5)

        # 1- Attempt to spend the frozen to non-recovery addr this should fail

        spendable = nodes[0].debugwallet({"list_frozen_outputs": True, "max_frozen_output_spendable": 1000})
        non_recovery_addr = nodes[1].getnewaddress()

        inputs = [{'tx': spendable["frozen_outputs"][0]["txid"], 'n': spendable["frozen_outputs"][0]["n"]}]
        outputs = [{
            'address': non_recovery_addr,
            'type': 'standard',
            'amount': spendable["frozen_outputs"][0]["amount"],
            'subfee': True
        }]

        coincontrol = {'spend_frozen_blinded': True, 'test_mempool_accept': True, "inputs": inputs}
        tx = nodes[0].sendtypeto('anon', 'part', outputs, 'comment', 'comment-to', 1, 1, False, coincontrol)
        assert_equal(tx["mempool-allowed"], False)
        assert_equal(tx["mempool-reject-reason"], "bad-frozen-spend-to-non-recovery")

        # Attempt to spend the frozen with ringsize > 1
        tx = nodes[0].sendtypeto('anon', 'part', outputs, 'comment', 'comment-to', 3, 1, False, coincontrol)
        assert_equal(tx["mempool-allowed"], False)
        assert_equal(tx["mempool-reject-reason"], "bad-frozen-ringsize")
        
        # 2- Spend the frozen to the recovery addr this should succeed
        recovery_addr = "pX9N6S76ZtA5BfsiJmqBbjaEgLMHpt58it"
        outputs[0]["address"] = recovery_addr
        tx = nodes[0].sendtypeto('anon', 'part', outputs, 'comment', 'comment-to', 1, 1, False, coincontrol)
        assert_equal(tx["mempool-allowed"], True)

        # 3- Spend non-frozen outputs to whereever and this should succeed
        #  - Create new anon outputs first 
        #  - Stake some blocks until greater than anonrestrictionstartheight
        #  - Spend new anon normally

        self.restart_nodes_with_anonoutputs()
        self.stop_nodes()

        self.start_node(0, ['-wallet=default_wallet', '-debug', '-blacklistedanon='+ tx_to_blacklist, '-anonrestrictionstartheight=6', '-lastanonindex=5'])
        self.start_node(1, ['-wallet=default_wallet', '-debug', '-blacklistedanon='+ tx_to_blacklist, '-anonrestrictionstartheight=6', '-lastanonindex=5'])
        self.connect_nodes_bi(0, 1)
        
        outputs = [{
            'address': non_recovery_addr,
            'type': 'standard',
            'amount': 100,
            'subfee': True
        }]

        # A->P
        coincontrol = {'test_mempool_accept': True}
        tx = nodes[0].sendtypeto('anon', 'part', outputs, 'comment', 'comment-to', 3, 1, False, coincontrol)
        assert_equal(tx["mempool-allowed"], True)

        # A->A
        coincontrol = {'test_mempool_accept': True}
        outputs[0]["address"] = nodes[1].getnewstealthaddress()
        tx = nodes[0].sendtypeto('anon', 'anon', outputs, 'comment', 'comment-to', 3, 1, False, coincontrol)

        assert_equal(tx["mempool-allowed"], True)

        # A->P

        coincontrol = {'test_mempool_accept': True}
        outputs[0]["address"] = nodes[1].getnewaddress()
        tx = nodes[0].sendtypeto('anon', 'part', outputs, 'comment', 'comment-to', 3, 1, False, coincontrol)
        assert_equal(tx["mempool-allowed"], True)

        # A->B
        coincontrol = {'test_mempool_accept': True}
        outputs[0]["address"] = nodes[1].getnewstealthaddress()
        tx = nodes[0].sendtypeto('anon', 'blind', outputs, 'comment', 'comment-to', 3, 1, False, coincontrol)
        assert_equal(tx["mempool-allowed"], True)


        # Blacklist everything
        # Create P->A tx
        # Attempt to spend the anon


        self.restart_nodes_with_anonoutputs()
        
        ao = nodes[0].anonoutput()["lastindex"]
        last_anon_index = ao

        tx_to_blacklist = (list)(range(1, last_anon_index + 1))
        tx_to_blacklist = ','.join(map(str, tx_to_blacklist))

        anon_restriction_start_height = nodes[0].anonoutput(output=str(last_anon_index))["blockheight"]
        self.stop_nodes()

        self.start_node(0, ['-wallet=default_wallet', '-debug', '-blacklistedanon='+ tx_to_blacklist, '-anonrestrictionstartheight='+ str(anon_restriction_start_height), '-lastanonindex='+ str(last_anon_index)])
        self.start_node(1, ['-wallet=default_wallet', '-debug', '-blacklistedanon='+ tx_to_blacklist, '-anonrestrictionstartheight='+ str(anon_restriction_start_height), '-lastanonindex='+ str(last_anon_index)])
        self.connect_nodes_bi(0, 1)

        outputs = [{
            'address': nodes[0].getnewstealthaddress(),
            'amount': 500,
            'subfee': True
        }]

        coincontrol = {'test_mempool_accept': True}
        tx = nodes[0].sendtypeto('part', 'anon', outputs, 'comment', 'comment-to', 3, 1, False, coincontrol)
        assert_equal(tx["mempool-allowed"], True)

        coincontrol = {'test_mempool_accept': True}
        tx = nodes[0].sendtypeto('part', 'anon', outputs, 'comment', 'comment-to', 3, 1, False, coincontrol)
        assert_equal(tx["mempool-allowed"], True)

        self.stakeBlocks(10)

        balances = nodes[0].getbalances()["mine"]
        assert balances["anon_trusted"] > 100

        # Now send back A->P
        outputs[0]["address"] = nodes[0].getnewaddress()
        
        unspent = nodes[0].listunspentanon(0, 9999)
        outputs[0]["amount"] = unspent[0]["amount"]

        inputs = [{'tx': unspent[0]["txid"], 'n': unspent[0]["vout"]}]
        coincontrol = {'test_mempool_accept': True, 'inputs': inputs}

        tx = nodes[0].sendtypeto('anon', 'part', outputs, 'comment', 'comment-to', 3, 1, False, coincontrol)
        assert_equal(tx["mempool-allowed"], True)

if __name__ == '__main__':
    ControlAnonTest4().main()
