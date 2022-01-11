#!/usr/bin/env python3
# Copyright (c) 2021 Ghost Core Team
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import decimal

from test_framework.test_particl import GhostTestFramework

from test_framework.util import (
    assert_equal,
    assert_greater_than
)


class ControlAnonTest(GhostTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2

        self.extra_args = [['-debug', '-anonrestricted=0', '-reservebalance=10000000', '-stakethreadconddelayms=500', '-txindex=1', '-maxtxfee=1', ] for i in range(self.num_nodes)]

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
                nodes[0].sendghosttoanon(node0_receiving_addr, 5, '', '', False, 'node0 -> node1 p->a')
                self.stakeBlocks(4)
            else:
                break
    def get_whole_amount_from_unspent(self, node, tx_hash):
        uns = node.listunspentanon(0, 9999)
        a = 0
        for s in uns:
            if s["txid"] == tx_hash:
                a = s["amount"]
                break
        return a

    def restart_nodes_with_anonoutputs(self):
        nodes = self.nodes
        self.stop_nodes()
        self.start_node(0, ['-wallet=default_wallet',  '-txindex', '-debug', '-anonrestricted=0', '-lastanonindex=0'])
        self.start_node(1, ['-wallet=default_wallet',  '-txindex','-debug', '-anonrestricted=0', '-lastanonindex=0'])
        self.connect_nodes_bi(0, 1)
        node1_receiving_addr = nodes[1].getnewstealthaddress()
        node0_receiving_addr = nodes[0].getnewstealthaddress()
        self.init_nodes_with_anonoutputs(nodes, node1_receiving_addr, node0_receiving_addr, 3)

        tx_to_blacklist = []
        lastanonindex = nodes[0].anonoutput()['lastindex']
        while lastanonindex > 0:
            tx_to_blacklist.append(lastanonindex)
            lastanonindex -= 1

        self.stop_nodes()
        tx_to_blacklist = ','.join(map(str, tx_to_blacklist))
        self.start_node(0, ['-wallet=default_wallet', '-maxtxfee=30', '-debug', '-anonrestricted=0',  '-txindex'])
        self.start_node(1, ['-wallet=default_wallet', '-maxtxfee=30', '-debug', '-txindex', '-blacklistedanon=' + tx_to_blacklist])
        self.connect_nodes_bi(0, 1)
        self.sync_all()

    def get_input_with_five(self, node, to_exclude_tx = None):
        unspents = node.listunspentanon(0, 9999)
        for unspent in unspents:
            if to_exclude_tx:
                if unspent["amount"] >= 1 and unspent["amount"] <= 5 and unspent["txid"] != to_exclude_tx:
                    return unspent
            else:
                if unspent["amount"] >= 1 and unspent["amount"] <= 5:
                    return unspent

        return False

    def is_unspent_eq_to_allow(self, unspent_txid, anon_details):
        return unspent_txid == anon_details["txnhash"]


    def run_test(self):
        nodes = self.nodes
        self.import_genesis_coins_a(nodes[0])
        self.import_genesis_coins_b(nodes[1])
        self.restart_nodes_with_anonoutputs()

        # Spending a blacklisted tx with fees greater than 10 * COIN and it should fail
        recovery_addr = "pX9N6S76ZtA5BfsiJmqBbjaEgLMHpt58it"
        unspent = nodes[1].listunspentanon(0, 9999, [], True, {"minimumAmount": 20})
        unspent_tx = unspent[0]
        inputs = [{'tx': unspent_tx["txid"], 'n': unspent_tx["vout"]}]
        coincontrol = {'test_fee': True, 'test_mempool_accept': True, 'spend_frozen_blinded': True, 'feeRate': 30, 'inputs': inputs}
        outputs = [{'address': recovery_addr, 'amount': unspent_tx["amount"], 'subfee': True}]
        
        tx = nodes[1].sendtypeto('anon', 'ghost', outputs, 'comment', 'comment-to', 1, 1, False, coincontrol)
        assert_greater_than(tx["fee"], 10)
        assert_equal(tx["mempool-reject-reason"], "bad-frozen-spend-fee-toolarge")

        # Shows now that spending the tx with fee less that 10 will succeed
        coincontrol["feeRate"] = 1
        tx = nodes[1].sendtypeto('anon', 'ghost', outputs, 'comment', 'comment-to', 1, 1, False, coincontrol)
        assert tx["fee"] < 10
        assert_equal(tx["mempool-allowed"], True)

        lastanonindex = nodes[0].anonoutput()['lastindex']
        tx_to_blacklist = (list)(range(1, lastanonindex+1))
        tx_to_blacklist = ','.join(map(str, tx_to_blacklist))

        self.stop_node(1, "Warning: -maxtxfee is set very high! Fees this large could be paid on a single transaction.")
        self.stop_node(0, "Warning: -maxtxfee is set very high! Fees this large could be paid on a single transaction.")

        # Attempt to spend a blacklisted tx to non-standard this will fail
        self.start_node(0, ['-wallet=default_wallet', '-txindex', '-blacklistedanon=' + tx_to_blacklist])
        self.start_node(1, ['-wallet=default_wallet', '-txindex', '-blacklistedanon=' + tx_to_blacklist])
        self.connect_nodes_bi(0, 1)

        unspents = nodes[0].listunspentanon(1, 9999, [], True, {"minimumAmount": 6})
        unspent2 = unspents[0]

        last_index_details = nodes[0].anonoutput(output=str(lastanonindex))

        for i in range(0, len(unspent2)):
            if self.is_unspent_eq_to_allow(unspent2["txid"], last_index_details):
                unspent2 = unspents[i]

        inputs = [{'tx': unspent2["txid"], 'n': unspent2["vout"]}]

        stealth_addr = nodes[1].getnewstealthaddress()
        coincontrol = {'spend_frozen_blinded': True, 'test_mempool_accept': True, 'inputs': inputs}
        outputs = [{'address': stealth_addr, 'amount': unspent2["amount"], 'subfee': True }]
        tx = nodes[0].sendtypeto('anon', 'anon', outputs, 'comment', 'comment-to', 1, 1, False, coincontrol)
        assert_equal(tx["mempool-reject-reason"], "bad-txns-frozen-blinded-out")

        # Attempt to spend blacklisted tx with ringsize > 1 this will fail
        outputs = [{'address': stealth_addr, 'amount': unspent2["amount"], 'subfee': True }]
        tx = nodes[0].sendtypeto('anon', 'ghost', outputs, 'comment', 'comment-to', 5, 1, False, coincontrol)
        assert_equal(tx["mempool-reject-reason"], "bad-frozen-ringsize")

        # Now shows that spending the inputs with ringsize=1 and to ghost will succeed
        outputs[0]["address"] = recovery_addr
        tx = nodes[0].sendtypeto('anon', 'ghost', outputs, 'comment', 'comment-to', 1, 1, False, coincontrol)
        assert_equal(tx["mempool-allowed"], True)

        # Spending less than 5 * COIN to recovery this will fail with bad-frozen-spend-toosmall
        unspent = nodes[0].listunspentanon(0, 9999, [], True, {"minimumAmount": 1, "maximumAmount": 5})
        input_with_five_amount = unspent[0] 
        inputs = [{'tx': input_with_five_amount["txid"], 'n': input_with_five_amount["vout"]}]
        coincontrol = {'test_mempool_accept': True, 'spend_frozen_blinded': True, 'inputs': inputs}
        outputs = [{'address': recovery_addr, 'amount': input_with_five_amount["amount"], 'subfee': True}]
        tx = nodes[0].sendtypeto('anon', 'ghost', outputs, 'comment', 'comment-to', 1, 1, False, coincontrol)
        assert_equal(tx["mempool-reject-reason"], "bad-frozen-spend-toosmall")

if __name__ == '__main__':
    ControlAnonTest().main()
